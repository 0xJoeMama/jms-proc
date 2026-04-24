# jms

A job managment system for POSIX.

Brought to you by:
- sdi2499168, Petros Plastiras

# How to
## Building
To build this project, you need to make GNU Make, a C++ compiler and a libc implementation on your system.

To build using Make and the default C++ compiler on your system, run:
```sh
$ make
```

To build using Make and your prefered C++ compiler, run:
```sh
$ make CXX=<insert-compiler-name>
```

Any of the above commands will build the whole project. This includes the `jms_coord` and `jms_console` executables, as well as:

To remove all build artifacts (including executables), run:
```sh
$ make clean
```

## Running
To run the project you first need to start a `jms_coord` process by providing a runtime path as well as the number 
of jobs every pool can spin up at most.

An example execution could be:
```sh
$ ./jms_coord -l run -n 12
```

The above will start a new coordinator with its runtime directory being `./run` and with a maximum of 12 jobs assigned to each pool.

Next, you can start up a `jms_console` instance, by providing the 2 FIFO files the coordinator created at startup, 
as well as an optional operations file to be read and streamed to the coordinator, line by line.

An example console execution would look like this:
```sh
$ ./jms_console -r ./run/jms_out -w ./run/jms_in -o inputs/sleeps_short.input
```

If you provided an operations file, the console will send all the commands from the input file to the coordinator.
After that, the console will enter interactive mode and which will allow you to directly input commands into the system.

After running a command, the console will show you the result of executing that command on the coordinator.

# Design
The system is split up into 3 important subsystems:
1. The console
2. The coordinator
3. The pools

## The console
The console is probably the simplest part of the whole system. It is a tool that pipes commands into the input of the coordinator and reads the responses from its output.

## The coordinator
The coordinator is the brain of the system. It is the one that parses and executes user issued commands (more on that later) by spawning and querying job pools.
Every job is assigned an increasing ID the moment a submit command for it is sent. Through that ID one can query every job in the history of the system.
The main job of the coordinator is to dispatch work to pools (or to fake dispatching work to pools) in order to find the result of a user command.

It keeps active FIFOs with all active pools. A pool is only created whenever the number of jobs in the previous pool is maxed out. 

The main loop of the coordinator consists of it waiting to form a connection or receive a command from an already formed connection with a console or receive a SIGCHLD from a pool (which means the pool has terminated).

When it receives a command, it parses it. For every kind of command that refers to a job, there are now 2 choices.
Either the pools that job was assigned to is still alive, or it has terminated. If the pool of the job has terminated, we can safely assume the job itself 
has done so as well, in which case we imitate the responses a pool would give in the coordinator, and no work is dispatched to pools. If the pool is still alive, a coordinator-issued command 
is sent to the pool and its response is used to form the response for the console.

If a job submission is sent to the coordinator, it assigns a job ID to that submission and then dispatches the launch of the job to a pool. The coordinator itself is not aware of the status of jobs.

## The pools
The pools have a main event loop like the following:
- Wait until either a coordinator-issued command is received or SIGTERM is received or SIGCHLD is received
1. On SIGTERM force stop all jobs and exit (this is the coordinator's mechanism of informing us of a system shutdown).
2. On SIGCHLD update the metadata of children to reflect their change in status.
3. On a coordinator-issued command, it executes that command and responds to the coordinator appropriately,

## Commands
A choice has been made for commands and responses to be simple strings. This allows for easier parsing and debugging.
For more insight looks into [this file](./include/command.hpp).

## Async-Engine
The whole system is built on top of an async-engine system implemented using C++ templates. 
It allows for asynchronous handling of events, driven by file descriptors. 
For more insight look into [this file](./include/poll.hpp).

# Design Assumptions
The Job Managment System has been built to properly function while the following preconditions hold:
1. **Only one console may be active at one time.** That means that the state of the system is 
   unspecified if more than one console tries to join concurrently with another one.
2. **Pools may not have spurious failures.** Pools exiting means all their jobs have exited. This means
   for example that killing a pool process manually or signaling termination through SIGTERM, 
   may leave the system in an inconsistent state.

There are ways to fix the above problems. Specifically:
- (1.) may be fixed using POSIX file locks.
- (2.) may be fixed by changing pool-job assignment calculations.

however, as we have been told in multiple Piazza discussions, no attempts will be made to break the system and this is simply a showcase of our capability to combine POSIX abstractions.
With more time however, both of those assumptions can be removed.
