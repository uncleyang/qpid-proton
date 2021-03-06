#ifndef PROTON_PROACTOR_H
#define PROTON_PROACTOR_H 1

/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <proton/condition.h>
#include <proton/event.h>
#include <proton/import_export.h>
#include <proton/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @copydoc proactor
 *
 * @addtogroup proactor Proactor
 *
 * The proactor associates an abstract AMQP protocol @ref connection with a
 * concrete IO @ref transport implementation for outgoing and incoming
 * connections. pn_proactor_wait() returns @ref proactor_events to application
 * threads for handling.
 *
 * The @ref pn_proactor* functions are thread-safe, but to handle @ref proactor_events you
 * must also use the @ref core APIs which are not. @ref core objects associated
 * with different connections can be used concurrently, but objects associated
 * with a single connection cannot.
 *
 * The proactor *serializes* @ref proactor_events for each connection - it never returns
 * @ref proactor_events for the same connection concurrently in different
 * threads. Event-handling code can safely use any @ref core object obtained
 * from the current event. You can attach application data to @ref core objects
 * (for example with pn_connection_attachments()).
 *
 * pn_connection_wake() allows any thread to "wake up" a connection. It causes
 * pn_proactor_wait() to return a @ref PN_CONNECTION_WAKE event that is
 * serialized with the connection's other @ref proactor_events. You can use this to implement
 * communication between different connections, or from non-proactor threads.
 *
 * Serialization and pn_connection_wake() simplify building applications with a
 * shared thread pool, which serialize work per connection. Many other
 * variations are possible, but you are responsible for any additional
 * synchronization needed.
 *
 * @addtogroup proactor
 * @{
 */

/**
 * Size of buffer that can hold the largest connection or listening address.
 */
#define PN_MAX_ADDR 1060

/**
 * Format a host:port address string for pn_proactor_connect() or pn_proactor_listen()
 *
 * @param[out] addr address is copied to this buffer, with trailing '\0'
 * @param[in] size  size of addr buffer
 * @param[in] host network host name, DNS name or IP address
 * @param[in] port network service name or decimal port number, e.g. "amqp" or "5672"
 * @return the length of network address (excluding trailing '\0'), if >= size
 * then the address was truncated
 */
PNP_EXTERN int pn_proactor_addr(char *addr, size_t size, const char *host, const char *port);

/**
 * Create a proactor. Must be freed with pn_proactor_free()
 */
PNP_EXTERN pn_proactor_t *pn_proactor(void);

/**
 * Free the proactor. Abort open connections/listeners, clean up all resources.
 */
PNP_EXTERN void pn_proactor_free(pn_proactor_t *proactor);

/**
 * Bind @p connection to a new @ref transport connected to @p addr.
 * Errors are returned as  @ref PN_TRANSPORT_CLOSED events by pn_proactor_wait().
 *
 * @note Thread safe.
 *
 * @param[in] proactor the proactor object
 *
 * @param[in] connection @p proactor *takes ownership* of @p connection and will
 * automatically call pn_connection_free() after the final @ref
 * PN_TRANSPORT_CLOSED event is handled, or when pn_proactor_free() is
 * called. You can prevent the automatic free with
 * pn_proactor_release_connection()
 *
 * @param[in] addr the "host:port" network address, constructed by pn_proactor_addr()
 * An empty host will connect to the local host via the default protocol (IPV6 or IPV4).
 * An empty port will connect to the standard AMQP port (5672).
 *
 * @param[in] connection @ref connection to be connected to @p addr.
 *
 */
PNP_EXTERN void pn_proactor_connect(pn_proactor_t *proactor, pn_connection_t *connection, const char *addr);

/**
 * Start listening for incoming connections.
 * Errors are returned as @ref proactor_events by pn_proactor_wait().
 *
 * @note Thread safe.
 *
 * @param[in] proactor the proactor object
 *
 * @param[in] listener @p proactor *takes ownership* of @p listener, and will
 * automatically call pn_listener_free() after the final PN_LISTENER_CLOSE event
 * is handled, or when pn_proactor_free() is called.
 *
 * @param[in] addr the "host:port" network address, constructed by pn_proactor_addr()
 * An empty host will listen for all protocols (IPV6 and IPV4) on all local interfaces.
 * An empty port will listen on the standard AMQP port (5672).

 * @param[in] backlog of un-handled connection requests to allow before refusing
 * connections. If @p addr resolves to multiple interface/protocol combinations,
 * the backlog applies to each separately.
 */
PNP_EXTERN void pn_proactor_listen(pn_proactor_t *proactor, pn_listener_t *listener, const char *addr, int backlog);

/**
 * Disconnect all connections and listeners belonging to the proactor.
 *
 * @ref PN_LISTENER_CLOSE, @ref PN_TRANSPORT_CLOSED and other @ref proactor_events are
 * generated as usual.  If no new listeners or connections are created, then a
 * @ref PN_PROACTOR_INACTIVE event will be generated when all connections and
 * listeners are disconnected.
 *
 * Note the proactor remains active, connections and listeners created after a call to
 * pn_proactor_disconnect() are not affected by it.
 *
 * @note Thread safe.
 *
 * @param proactor the proactor
 *
 * @param condition if not NULL the condition data is copied to each
 * disconnected transports and listener and is available in the close event.
 */
PNP_EXTERN void pn_proactor_disconnect(pn_proactor_t *proactor, pn_condition_t *condition);

/**
 * Wait until there are @ref proactor_events to handle.
 *
 * You must call pn_proactor_done() when you are finished with the batch, you
 * must not use the batch pointer after calling pn_proactor_done().
 *
 * Normally it is most efficient to handle the entire batch in the calling
 * thread and then call pn_proactor_done(), but see pn_proactor_done() for more options.
 *
 * pn_proactor_get() is a non-blocking version of this call.
 *
 * @note Thread Safe.
 *
 * @return a non-empty batch of events that must be processed in sequence.
 *
 */
PNP_EXTERN pn_event_batch_t *pn_proactor_wait(pn_proactor_t *proactor);

/**
 * Return @ref proactor_events if any are available immediately.  If not, return NULL.
 * If the return value is not NULL, the behavior is the same as pn_proactor_wait()
 *
 * @note Thread Safe.
 */
PNP_EXTERN pn_event_batch_t *pn_proactor_get(pn_proactor_t *proactor);

/**
 * Call when finished handling a batch of events.
 *
 * Must be called exactly once to match each call to pn_proactor_wait().
 *
 * @note Thread-safe: May be called from any thread provided the exactly once
 * rule is respected.
 */
PNP_EXTERN void pn_proactor_done(pn_proactor_t *proactor, pn_event_batch_t *events);

/**
 * Return a @ref PN_PROACTOR_INTERRUPT event as soon as possible.
 *
 * Exactly one @ref PN_PROACTOR_INTERRUPT event is generated for each call to
 * pn_proactor_interrupt().  If threads are blocked in pn_proactor_wait(), one
 * of them will be interrupted, otherwise the interrupt will be returned by a
 * future call to pn_proactor_wait(). Calling pn_proactor_interrupt().
 *
 * @note Thread safe
 */
PNP_EXTERN void pn_proactor_interrupt(pn_proactor_t *proactor);

/**
 * Return a @ref PN_PROACTOR_TIMEOUT after @p timeout milliseconds elapse. If no
 * threads are blocked in pn_proactor_wait() when the timeout elapses, the event
 * will be delivered to the next available thread.
 *
 * Calling pn_proactor_set_timeout() again before the PN_PROACTOR_TIMEOUT
 * is delivered will cancel the previous timeout and deliver an event only after
 * the new timeout.
 *
 * @note Thread safe
 */
PNP_EXTERN void pn_proactor_set_timeout(pn_proactor_t *proactor, pn_millis_t timeout);

/**
 * Cancel the pending timeout set by pn_proactor_set_timeout(). Does nothing
 * if no timeout is set.
 *
 * @note Thread safe
 */
PNP_EXTERN void pn_proactor_cancel_timeout(pn_proactor_t *proactor);

/**
 * Release ownership of @p connection, disassociate it from its proactor.
 *
 * The connection and related objects (@ref session "sessions", @ref link "links"
 * and so on) remain intact, but the transport is closed and unbound. The
 * proactor will not return any more events for this connection. The caller must
 * call pn_connection_free(), either directly or indirectly by re-using @p
 * connection in another call to pn_proactor_connect() or pn_proactor_listen().
 *
 * @note **NOT** thread safe, call from connection event handler.
 *
 * @note If @p connection does not belong to a proactor, this call does nothing.
 *
 * @note This has nothing to do with pn_connection_release()
 */
PNP_EXTERN void pn_proactor_release_connection(pn_connection_t *connection);

/**
 * Return a @ref PN_CONNECTION_WAKE event for @p connection as soon as possible.
 *
 * At least one wake event will be returned, serialized with other @ref proactor_events
 * for the same connection.  Wakes can be "coalesced" - if several
 * pn_connection_wake() calls happen close together, there may be only one
 * PN_CONNECTION_WAKE event that occurs after all of them.
 *
 * @note If @p connection does not belong to a proactor, this call does nothing.
 *
 * @note Thread safe
 */
PNP_EXTERN void pn_connection_wake(pn_connection_t *connection);

/**
 * Return the proactor associated with a connection.
 *
 * @note Not Thread safe.
 *
 * @return the proactor or NULL if the connection does not belong to a proactor.
 */
PNP_EXTERN pn_proactor_t *pn_connection_proactor(pn_connection_t *connection);

/**
 * Return the proactor associated with an event.
 *
 * @note Not Thread safe.
 *
 * @return the proactor or NULL if the connection does not belong to a proactor.
 */
PNP_EXTERN pn_proactor_t *pn_event_proactor(pn_event_t *event);

/**
 * Get the real elapsed time since an arbitrary point in the past in milliseconds.
 *
 * This may be used as a portable way to get a timestamp for the current time. It is monotonically
 * increasing and will never go backwards.
 *
 * @note Thread safe.
 */
PNP_EXTERN pn_millis_t pn_proactor_now(void);

/**
 * @defgroup proactor_events Events
 *
 * **Experimental** - Events returned by pn_proactor_wait().
 * pn_proactor_wait() returns a subset of the event types defined by @ref pn_event_type_t.
 * The PN_REACTOR_..., PN_SELECTABLE_... and PN_..._FINAL events are not returned.
 *
 * Enumeration | Brief description, see @ref pn_event_type_t for more
 * :-- | :--
 * @ref PN_CONNECTION_INIT | @copybrief PN_CONNECTION_INIT
 * @ref PN_CONNECTION_BOUND |  @copybrief PN_CONNECTION_BOUND
 * @ref PN_TIMER_TASK | @copybrief PN_TIMER_TASK
 * @ref PN_CONNECTION_INIT | @copybrief PN_CONNECTION_INIT
 * @ref PN_CONNECTION_BOUND | @copybrief PN_CONNECTION_BOUND
 * @ref PN_CONNECTION_UNBOUND | @copybrief PN_CONNECTION_UNBOUND
 * @ref PN_CONNECTION_LOCAL_OPEN | @copybrief PN_CONNECTION_LOCAL_OPEN
 * @ref PN_CONNECTION_REMOTE_OPEN | @copybrief PN_CONNECTION_REMOTE_OPEN
 * @ref PN_CONNECTION_LOCAL_CLOSE | @copybrief PN_CONNECTION_LOCAL_CLOSE
 * @ref PN_CONNECTION_REMOTE_CLOSE | @copybrief PN_CONNECTION_REMOTE_CLOSE
 * @ref PN_SESSION_INIT | @copybrief PN_SESSION_INIT
 * @ref PN_SESSION_LOCAL_OPEN | @copybrief PN_SESSION_LOCAL_OPEN
 * @ref PN_SESSION_REMOTE_OPEN | @copybrief PN_SESSION_REMOTE_OPEN
 * @ref PN_SESSION_LOCAL_CLOSE | @copybrief PN_SESSION_LOCAL_CLOSE
 * @ref PN_SESSION_REMOTE_CLOSE | @copybrief PN_SESSION_REMOTE_CLOSE
 * @ref PN_LINK_INIT | @copybrief PN_LINK_INIT
 * @ref PN_LINK_LOCAL_OPEN | @copybrief PN_LINK_LOCAL_OPEN
 * @ref PN_LINK_REMOTE_OPEN | @copybrief PN_LINK_REMOTE_OPEN
 * @ref PN_LINK_LOCAL_CLOSE | @copybrief PN_LINK_LOCAL_CLOSE
 * @ref PN_LINK_REMOTE_CLOSE | @copybrief PN_LINK_REMOTE_CLOSE
 * @ref PN_LINK_LOCAL_DETACH | @copybrief PN_LINK_LOCAL_DETACH
 * @ref PN_LINK_REMOTE_DETACH | @copybrief PN_LINK_REMOTE_DETACH
 * @ref PN_LINK_FLOW | @copybrief PN_LINK_FLOW
 * @ref PN_DELIVERY | @copybrief PN_DELIVERY
 * @ref PN_TRANSPORT | @copybrief PN_TRANSPORT
 * @ref PN_TRANSPORT_AUTHENTICATED | @copybrief PN_TRANSPORT_AUTHENTICATED
 * @ref PN_TRANSPORT_ERROR | @copybrief PN_TRANSPORT_ERROR
 * @ref PN_TRANSPORT_HEAD_CLOSED | @copybrief PN_TRANSPORT_HEAD_CLOSED
 * @ref PN_TRANSPORT_TAIL_CLOSED | @copybrief PN_TRANSPORT_TAIL_CLOSED
 * @ref PN_TRANSPORT_CLOSED | The final event for a proactor connection, the transport is closed.
 * @ref PN_LISTENER_OPEN | @copybrief PN_LISTENER_OPEN
 * @ref PN_LISTENER_ACCEPT | @copybrief PN_LISTENER_ACCEPT
 * @ref PN_LISTENER_CLOSE | @copybrief PN_LISTENER_CLOSE
 * @ref PN_PROACTOR_INTERRUPT | @copybrief PN_PROACTOR_INTERRUPT
 * @ref PN_PROACTOR_TIMEOUT | @copybrief PN_PROACTOR_TIMEOUT
 * @ref PN_PROACTOR_INACTIVE | @copybrief PN_PROACTOR_INACTIVE
 * @ref PN_CONNECTION_WAKE | @copybrief PN_CONNECTION_WAKE
 *
 * @}
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* proactor.h */
