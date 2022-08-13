#include <sstream>
#include <cctype>
#include <cassert>
#include "csapp.h"
#include "message.h"
#include "connection.h"
#include <iostream>

Connection::Connection()
    : m_fd(-1), m_last_result(SUCCESS)
{
}

Connection::Connection(int fd)
    : m_fd(fd), m_last_result(SUCCESS)
{
  // TODO: call rio_readinitb to initialize the rio_t object
  rio_readinitb(&m_fdbuf, fd);
}

void Connection::connect(const std::string &hostname, int port)
{
  // TODO: call open_clientfd to connect to the server
  m_fd = open_clientfd(hostname.c_str(), std::to_string(port).c_str());
  // TODO: call rio_readinitb to initialize the rio_t object
  rio_readinitb(&m_fdbuf, m_fd);
}

Connection::~Connection()
{
  // TODO: close the socket if it is open
  if (is_open())
  {
    close();
  }
  m_fd = -1;
}

bool Connection::is_open() const
{
  // TODO
  return m_fd >= 0;
}

void Connection::close()
{
  Close(m_fd);
}

bool Connection::send(const Message &msg)
{
  // TODO: send a message
  // return true if successful, false if not
  // make sure that m_last_result is set appropriately
  m_last_result = INVALID_MSG;
  if (msg.tag.length() + msg.data.length() + 2 > msg.MAX_LEN)
  {
    return false;
  }
  if (!is_valid_tag(msg.tag))
  {
    return false;
  }
  if (msg.tag == TAG_DELIVERY && !is_valid_delivery(msg))
  {
    return false;
  }
  if ((msg.tag == TAG_ERR || msg.tag == TAG_OK || msg.tag == TAG_SLOGIN || msg.tag == TAG_RLOGIN || msg.tag == TAG_JOIN || msg.tag == TAG_SENDALL) &&
      (msg.data == ""))
  {
    return false;
  }
  if (rio_writen(m_fd, msg.tag.c_str(), msg.tag.length()) == -1 || rio_writen(m_fd, ":", 1) == -1 || rio_writen(m_fd, msg.data.c_str(), msg.data.length()) == -1 || rio_writen(m_fd, "\n", 1) == -1)
  {
    m_last_result = EOF_OR_ERROR;
    return false;
  }
  m_last_result = SUCCESS;
  return true;
}

bool Connection::receive(Message &msg)
{
  // TODO: send a message, storing its tag and data in msg
  // return true if successful, false if not
  // make sure that m_last_result is set appropriately
  char buf[255];
  ssize_t result = rio_readlineb(&m_fdbuf, buf, sizeof(buf));
  if (result == 0 || result == -1)
  {
    m_last_result = EOF_OR_ERROR;
    return false;
  }

  m_last_result = INVALID_MSG;
  std::string message = std::string(buf);
  std::string tag = message.substr(0, message.find(":"));
  if (!is_valid_tag(tag))
  {
    return false;
  }
  std::string data = message.substr(message.find(":") + 1);
  msg.tag = tag;
  msg.data = data;
  if (tag == TAG_DELIVERY && !is_valid_delivery(msg))
  {
    return false;
  }
  if ((tag == TAG_ERR || tag == TAG_OK || tag == TAG_SLOGIN || tag == TAG_RLOGIN || tag == TAG_JOIN || tag == TAG_SENDALL) &&
      (data == ""))
  {
    return false;
  }
  if (data.back() == '\n')
  {
    msg.data.pop_back();
  }
  if (data.back() == '\r')
  {
    msg.data.pop_back();
  }
  m_last_result = SUCCESS;
  return true;
}

bool Connection::is_valid_tag(std::string tag)
{
  return (tag == TAG_ERR || tag == TAG_OK || tag == TAG_SLOGIN || tag == TAG_RLOGIN || tag == TAG_JOIN || tag == TAG_LEAVE ||
          tag == TAG_SENDALL || tag == TAG_SENDUSER || tag == TAG_QUIT || tag == TAG_DELIVERY || tag == TAG_EMPTY);
}

bool Connection::is_valid_delivery(Message msg)
{
  return msg.split_payload().size() == 3;
}