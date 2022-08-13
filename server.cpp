#include <pthread.h>
#include <iostream>
#include <sstream>
#include <memory>
#include <set>
#include <vector>
#include <cctype>
#include <cassert>
#include "message.h"
#include "connection.h"
#include "user.h"
#include "room.h"
#include "guard.h"
#include "server.h"

////////////////////////////////////////////////////////////////////////
// Server implementation data types
////////////////////////////////////////////////////////////////////////

struct ConnInfo
{
  Connection *conn;
  Server *server;

  ConnInfo(Connection *conn, Server *server) : conn(conn), server(server) {}
  ~ConnInfo()
  {
    // destroy connection when ConnInfo object is destroyed
    delete conn;
  }
};

////////////////////////////////////////////////////////////////////////
// Client thread functions
////////////////////////////////////////////////////////////////////////

namespace
{
  int chat_with_reciever(User *thisUser, Room **room, ConnInfo *info);
  int chat_with_sender(User *thisUser, Room **room, ConnInfo *info);
  void *worker(void *arg)
  {
    pthread_detach(pthread_self());

    ConnInfo *info_ = static_cast<ConnInfo *>(arg);

    // use a std::unique_ptr to automatically destroy the ConnInfo object
    // when the worker function finishes; this will automatically ensure
    // that the Connection object is destroyed
    std::unique_ptr<ConnInfo> info(info_);
    std::string client = "";

    Message msg;

    if (!info->conn->receive(msg))
    {
      if (info->conn->get_last_result() == Connection::INVALID_MSG)
      {
        info->conn->send(Message(TAG_ERR, "invalid message format"));
      }
      return nullptr;
    }

    if (msg.tag != TAG_SLOGIN && msg.tag != TAG_RLOGIN)
    {
      info->conn->send(Message(TAG_ERR, "did not receive valid login message"));
      return nullptr;
    }
    if (msg.tag == TAG_SLOGIN)
    {
      client = "sender";
    }
    else
    {
      client = "reciever";
    }
    std::string username = msg.data;
    if (username == "")
    {
      info->conn->send(Message(TAG_ERR, "did not receive valid login message"));
      return nullptr;
    }

    if (!info->conn->send(Message(TAG_OK, "logged in as " + username)))
    {
      return nullptr;
    }
    User *thisUser = new User(username);
    Room *room = nullptr;
    // Just loop reading messages and sending an ok response for each one
    while (true)
    {
      if (client == "reciever")
      {
        if(chat_with_reciever(thisUser, &room, info_) != 0)
        {
          break;
        }
      }
      else
      {
        if(chat_with_sender(thisUser, &room, info_) != 0)
        {
          break;
        }
      }
    }
    delete thisUser;
    return nullptr;
  }
  int chat_with_reciever(User *thisUser, Room **room, ConnInfo *info)
  {
    Message msg;
    if (*room == nullptr)
    {
      if (!info->conn->receive(msg))
      {
        if (info->conn->get_last_result() == Connection::INVALID_MSG)
        {
          info->conn->send(Message(TAG_ERR, "invalid message format"));
          return 1;
        }
      }
      if (msg.data.length() > msg.MAX_LEN)
      {
        info->conn->send(Message(TAG_ERR, "invalid message format"));
        return 1; // Invalid Message Format
      }
      if (msg.tag != TAG_JOIN)
      {
        info->conn->send(Message(TAG_ERR, "invalid message format"));
        return 1; // Not in room
      }
      else
      {
        *room = info->server->find_or_create_room(msg.data);
        (*room)->add_member(thisUser);
        info->conn->send(Message(TAG_OK, "joined room " + msg.data));
        if(info->conn->get_last_result() == Connection::EOF_OR_ERROR)
        {
          (*room)->remove_member(thisUser);
          return 1;
        }
      }
    }
    else
    {
      // Send message if there is a message to be broadcasted
      Message *broadcastMsg = thisUser->mqueue.dequeue();
      if(broadcastMsg != nullptr) {
        std::vector<std::string> contents = broadcastMsg->split_payload();
        if (contents.at(1) != thisUser->username && contents.at(0) == (*room)->get_room_name())
        {

          info->conn->send(*broadcastMsg);
        }
        delete broadcastMsg;
      }
    }
    return 0;
  }

  // Returns 1 if terminates, 0 if doesn't need to terminate
  int chat_with_sender(User *thisUser, Room **room, ConnInfo *info)
  {
    Message msg;
    if (!info->conn->receive(msg))
    {
      if (info->conn->get_last_result() == Connection::INVALID_MSG)
      {
        info->conn->send(Message(TAG_ERR, "invalid message format"));
      }
    }
    if (msg.data.length() > msg.MAX_LEN)
    {
      info->conn->send(Message(TAG_ERR, "invalid message format"));
    }
    if (*room == nullptr)
    {
      if(msg.tag == TAG_QUIT)
      {
        info->conn->send(Message(TAG_OK, "bye"));
        return 1;
      }
      if (msg.tag != TAG_JOIN)
      {
        info->conn->send(Message(TAG_ERR, "not in a room"));
      }
      else
      {
        *room = info->server->find_or_create_room(msg.data);
        (*room)->add_member(thisUser);
        info->conn->send(Message(TAG_OK, "joined room " + msg.data));
      }
    }
    else
    {
      if(msg.tag == TAG_LEAVE || msg.tag == TAG_JOIN || msg.tag == TAG_QUIT)
      {
        (*room)->remove_member(thisUser);
        *room = nullptr;
        info->conn->send(Message(TAG_OK, "left room"));
      }
      if(msg.tag == TAG_JOIN)
      {
        *room = info->server->find_or_create_room(msg.data);
        (*room)->add_member(thisUser);
        info->conn->send(Message(TAG_OK, "joined room " + msg.data));
      }
      if(msg.tag == TAG_QUIT)
      {
        info->conn->send(Message(TAG_OK, "bye"));
        return 1;
      }
      if(msg.tag == TAG_SENDALL)
      {
        (*room)->broadcast_message(thisUser->username, msg.data);
        info->conn->send(Message(TAG_OK, "message sent"));
      }
    }
    return 0;
  }
}

////////////////////////////////////////////////////////////////////////
// Server member function implementation
////////////////////////////////////////////////////////////////////////

Server::Server(int port)
    : m_port(port), m_ssock(-1)
{
  pthread_mutex_init(&m_lock, nullptr);
}

Server::~Server()
{
  pthread_mutex_destroy(&m_lock);
}

bool Server::listen()
{
  std::string port = std::to_string(m_port);
  m_ssock = open_listenfd(port.c_str());
  return m_ssock >= 0;
}

void Server::handle_client_requests()
{
  assert(m_ssock >= 0);

  while (true)
  {
    int clientfd = accept(m_ssock, nullptr, nullptr);
    if (clientfd < 0)
    {
      std::cerr << "Error accepting connection\n";
      return;
    }

    ConnInfo *info = new ConnInfo(new Connection(clientfd), this);

    pthread_t thr_id;
    if (pthread_create(&thr_id, nullptr, worker, static_cast<void *>(info)) != 0)
    {
      std::cerr << "Could not create thread\n";
      return;
    }
  }
}

Room *Server::find_or_create_room(const std::string &room_name)
{
  // this function can be called from multiple threads, so
  // make sure the mutex is held while accessing the shared
  // data (the map of room names to room objects)
  Guard g(m_lock);

  Room *room;

  auto i = m_rooms.find(room_name);
  if (i == m_rooms.end())
  {
    // room does not exist yet, so create it and add it to the map
    room = new Room(room_name);
    m_rooms[room_name] = room;
  }
  else
  {
    room = i->second;
  }
  return room;
}