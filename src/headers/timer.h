/**
 * This file contains type definitions, constant definitions and function declarations specific to the Timer
 */

#ifndef DOMOTICS_TIMER_H
#define DOMOTICS_TIMER_H

#include "return_codes.h"
#include "messages.h"
#include "utils.h"
#include "routing.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>

// Default values

#define MAX_TIMER_ARGUMENTS     4    //state, on/open time of the child, begin and end
#define MAX_PENDING_REQUESTS    8    //commands that can be awaiting a child reply at the same time
#define MINUTES_IN_A_DAY        1440 //begin and end are minutes from midnight, so they must be less than this
#define SECONDS_IN_A_DAY        (MINUTES_IN_A_DAY * 60) //used by the schedule thread to wait until the next day
#define DEFAULT_BEGIN           0    //midnight, so any end is always greater than the default begin
#define DEFAULT_END             (MINUTES_IN_A_DAY - 1) //23:59, so any begin is always smaller than the default end

/**
 * Main function of the Timer Program
 * @param argc Number of arguments received
 * @param argv Argument vector of length `argc`, each string is terminated by `'\0'`
 *
 * @returns `MISSING_ID_ARGUMENT` if the ID command-line argument is missing,
 * `UNABLE_TO_OPEN_PIPE` if the IPC pipes could not be opened,
 * `UNABLE_TO_CLOSE_PIPE` if the IPC pipes could not be closed,
 * `OK` otherwise
 */
int main(int argc, char *argv[]);

/**
 * Handles the shutdown also cleaning up IPC files, best practice to do
 * @param error The error code that caused the shutdown, returned if the cleanup succeeds
 */
void handle_shutdown(error_code_t error);

/**
 * Handles the shutdown caused by a `SIGTERM` signal
 * @param sig_num Number of the received signal, unused
 */
void sigterm_handler(int sig_num);

/**
 * Locks the mutex that guards the data shared between the threads
 *
 * The mutex is statically initialized, so a failure means the shared state is no longer reliable:
 * the error is printed and the exit is requested, the shutdown is then performed by the main thread
 */
void lock_data();

/**
 * Unlocks the mutex that guards the data shared between the threads
 *
 * On failure the error is printed and the exit is requested, as for `lock_data`
 */
void unlock_data();

/**
 * Records a command sent to the child among the ones whose reply is still awaited
 *
 * The caller must already hold the lock
 *
 * @param code The command code sent to the child
 *
 * @returns `true` if the command was recorded, `false` if there is no room left
 */
bool add_pending(command_code_t code);

/**
 * Removes the first awaited command matching the given one, keeping the order of the remaining ones
 *
 * The match is done on the command and not on the oldest entry, because a child that is a control device
 * can complete commands of different kinds out of order
 *
 * The caller must already hold the lock
 *
 * @param code The command code of the received reply
 *
 * @returns `true` if a matching awaited command was found and removed, `false` otherwise
 */
bool take_pending(command_code_t code);

/**
 * Reads the pipe and identifies the command
 * @returns the error code to handle for the response
 */
error_code_t read_pipe();

/**
 * Writes to the pipe
 */
void write_pipe();

/**
 * Sends an already formatted response up to the parent, guarding the pipe shared between the threads
 *
 * `SIGPIPE` is ignored, so a write to a pipe with no reader fails instead of terminating the process:
 * towards the parent there is nobody left to report the failure to, so the timer shuts down
 *
 * @param buffer The formatted response, of size `MAX_RESPONSE_SIZE`
 * @param message Additional message for the error printing, if the write fails
 */
void write_to_parent(char *buffer, char *message);

/**
 * Forgets the child, freeing its subtree and closing its pipe, and goes back to the plain timer type
 *
 * The caller must already hold the lock
 *
 * @returns `UNABLE_TO_CLOSE_PIPE` if the child requests pipe could not be closed, `OK` otherwise
 */
error_code_t drop_child();

/**
 * Drops the child after a failed write towards it, meaning nobody is reading its pipe anymore
 *
 * The timer keeps working and reports the failure, instead of terminating the way a leaf device does
 */
void release_unreachable_child();

/**
 * Acquires the child if the response is its change-parent response naming the timer as the new parent
 *
 * Sets the child scalars and the declared type under the lock, does nothing for any other response
 *
 * @param child_response The response read from the child
 */
void acquire_child(response_t *child_response);

/**
 * Records a deeper descendant (whose parent is not the timer) in the routing table under its own parent,
 * so the whole subtree can be replayed to a new parent, does nothing for a response that is not a descendant
 * change-parent or for a device that is not part of the timer subtree
 *
 * @param child_response The response read from the child
 */
void acquire_descendant(response_t *child_response);

/**
 * Releases the device that sent a delete response, because it no longer exists: the direct child frees the
 * scalars and the declared type, a tracked descendant is removed from the routing table so the subtree
 * replay does not re-announce deleted nodes
 *
 * Does nothing for a response that is not a delete
 *
 * @param child_response The response read from the child
 */
void release_child(response_t *child_response);

/**
 * Bottom-up thread body: reads the responses coming from the child and forwards them up to the parent
 * @param arg Unused
 * @returns `NULL`
 */
void *child_responses_handler(void *arg);

/**
 * Turns a child reply to a request the timer made for mirroring into the timer own response and sends it up
 *
 * @param child_response The response read from the child
 *
 * @returns `true` if the response was the awaited reply and has been handled (must not be forwarded),
 * `false` otherwise
 */
bool handle_own_reply(response_t *child_response);

/**
 * Builds the switch command for the child based on its type and sends it down the child requests pipe
 *
 * @param activate `true` to switch the child on (or open), `false` to switch it off (or close)
 *
 * @returns `CHILD_NOT_FOUND` if there is no child, `UNABLE_TO_WRITE_PIPE` if the request could not be sent,
 * a format error code if the request could not be formatted, `OK` otherwise
 */
error_code_t send_child_switch(bool activate);

/**
 * Schedule thread body: sleeps until the next begin or end time and switches the child on or off, each day
 * @param arg Unused
 * @returns `NULL`
 */
void *schedule_handler(void *arg);

/**
 * Cancels the schedule thread and starts a new one, so it re-reads the current begin and end times
 */
void reschedule();

/**
 * Executes the command received from the pipe
 * @returns the error code that occurred while reading the request, `OK` otherwise
 */
error_code_t execute_command();

/**
 * Sets the attributes for the info response
 */
void create_info_response();

/**
 * Sets the attributes for the link response and performs actions if needed
 */
void create_link_response();

/**
 * Sends a faked change-parent response on behalf of the child, so a new parent rebuilds the branch
 *
 * Does nothing if the timer has no child
 */
void replay_child_add();

/**
 * Replays the whole tracked subtree to the new parent, sending a faked change-parent response for each node
 * (top to bottom, a node always after its parent), so the new parent rebuilds every branch below the timer
 *
 * Used when the child is a control device, the leaf-child case is handled by `replay_child_add`
 */
void replay_subtree();

/**
 * Sets the attributes for the registry response and performs actions if needed
 */
void create_registry_response();

/**
 * Sets the attributes for the switch response and performs actions if needed
 */
void create_switch_response();

/**
 * Propagates the delete to the child, so the whole branch below the timer is terminated
 *
 * The response is deferred to the child confirmation, which also triggers the shutdown; if there is no child,
 * or the request could not be sent, the exit is requested immediately and the response is sent by the caller
 */
void create_delete_response();

/**
 * Opens in writing the down pipe of the child (the child itself created it), so the timer can send requests to it
 *
 * It is the down-going twin of `change_snd_responses_pipe`, could be moved to `utils` to be shared with the Hub
 *
 * @param child_id The id of the child
 * @param snd_requests_child_fd Pointer where the function will put the file descriptor to send requests to the child
 *
 * @returns `UNABLE_TO_OPEN_PIPE` if the pipe could not be opened, `OK` otherwise
 */
error_code_t open_child_requests_pipe(device_id_t child_id, int *snd_requests_child_fd);

#endif
