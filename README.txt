Sample README.txt

Eventually your report about how you implemented thread synchronization
in the server should go here

First, we determined the shared data structures that would need to be protected
in order to prevent issues with modifications at the same time. Working from the
most encompassing data structure down, we started with the Server which contains
a map of room names to room pointers. Since multiple threads might be creating rooms and
adding them to the map at the same time,  the code for creating rooms is a critical section. 
Thus, we used a mutex, so only one thread can modify and rooms to the map at a time. Next, we 
looked at Room, which contained a set of User pointers. The two functions add and remove members
are critical sections, so we implemented a mutex here as well to make sure only one thread can
modify the set at a time. Finally, the User class contains a message queue. The two functions
that modify this queue are enqueue and dequeue, so those are our critical sections. First, 
a mutex was implemented so only one thread can modify the queue at one time. Second, we needed
a way to allow sender threads to add messages to the queue and then notify all reciever threads
that a new message is in the queue for them to dequeue, as well as a way to allow reciever threads
to get any available messages and then decrement the number of available messages by 1, so it doesn't 
try to read more messages in the queue if there are non available. To do this, we used a semaphore that
counts the number of available messages. It is initalized to 0 and whenever a message is enqueued, sem_post
is called to increase the number of available messages by 1 and notify all sender threads that are calling
sem_timedwait that a message is available. Dequeue calls sem_timedwait which will get the message and decrement
available messages by 1, only waiting up to 1 second. Since those are all the shared data sections that are synchronized
using mutexes and semaphores, we were able to make the server run successfully with multiple threads.