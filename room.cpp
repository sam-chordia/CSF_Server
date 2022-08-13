#include "guard.h"
#include "message.h"
#include "message_queue.h"
#include "user.h"
#include "room.h"

Room::Room(const std::string &room_name)
  : room_name(room_name) {
  pthread_mutex_init(&lock, nullptr);
}

Room::~Room() {
  pthread_mutex_destroy(&lock);
}

void Room::add_member(User *user) {
  Guard guard(lock);
  members.insert(user);
}

void Room::remove_member(User *user) {
  Guard guard(lock);
  members.erase(user);
}

void Room::broadcast_message(const std::string &sender_username, const std::string &message_text) {
  Guard guard(lock);
  std::stringstream ss;
  ss << room_name << ":" << sender_username << ":" << message_text;
  for(UserSet::iterator itr = members.begin(); itr != members.end(); itr++) 
  {
    Message *msg = new Message(TAG_DELIVERY, ss.str());
    (*itr) -> mqueue.enqueue(msg);
  }
}
