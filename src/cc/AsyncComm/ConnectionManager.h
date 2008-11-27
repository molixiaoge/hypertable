/**
 * Copyright (C) 2008 Doug Judd (Zvents, Inc.)
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef HYPERTABLE_CONNECTIONMANAGER_H
#define HYPERTABLE_CONNECTIONMANAGER_H

#include <queue>
#include <string>

#include <boost/shared_ptr.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>

extern "C" {
#include <time.h>
#include <sys/time.h>
}

#include "Common/ReferenceCount.h"
#include "Common/SockAddrMap.h"
#include "Common/Timer.h"

#include "Comm.h"
#include "DispatchHandler.h"

namespace Hypertable {

  class Event;

  /**
   * Establishes and maintains a set of TCP connections.  If any of the
   * connections gets broken, then this class will continuously attempt
   * to re-establish the connection, pausing for a while in between attempts.
   */
  class ConnectionManager : public DispatchHandler {

  public:

    class ConnectionState : public ReferenceCount {
    public:
      bool                connected;
      struct sockaddr_in  addr;
      struct sockaddr_in  local_addr;
      uint32_t            timeout_millis;
      DispatchHandlerPtr  handler;
      Mutex               mutex;
      boost::condition    cond;
      boost::xtime        next_retry;
      std::string         service_name;
    };
    typedef boost::intrusive_ptr<ConnectionState> ConnectionStatePtr;


    struct LtConnectionState {
      bool operator()(const ConnectionStatePtr &cs1,
                      const ConnectionStatePtr &cs2) const {
        return xtime_cmp(cs1->next_retry, cs2->next_retry) >= 0;
      }
    };

    struct SharedImpl {
      Comm              *comm;
      Mutex              mutex;
      boost::condition   retry_cond;
      boost::thread     *thread;
      SockAddrMap<ConnectionStatePtr>  conn_map;
      std::priority_queue<ConnectionStatePtr, std::vector<ConnectionStatePtr>,
          LtConnectionState> retry_queue;
      bool quiet_mode;
    };

    /**
     * Constructor.  Creates a thread to do connection retry attempts.
     *
     * @param comm Pointer to the comm object
     */
    ConnectionManager(Comm *comm = 0) {
      m_impl = new SharedImpl;
      m_impl->comm = comm ? comm : Comm::instance();
      m_impl->thread = new boost::thread(*this);
      m_impl->quiet_mode = false;
    }

    /**
     * Copy Constructor.  Shares the implementation with object being copied.
     */
    ConnectionManager(const ConnectionManager &cm) {
      m_impl = cm.m_impl;
      intrusive_ptr_add_ref(this);
    }

    /**
     * Destructor.  TBD.
     */
    virtual ~ConnectionManager() {
      return;
    }

    /**
     * Adds a connection to the connection manager.  The address structure addr
     * holds an address that the connection manager should maintain a
     * connection to.  This method first checks to see if the address is
     * already registered with the connection manager and returns immediately
     * if it is.  Otherwise, it adds the address to an internal connection map,
     * attempts to establish a connection to the address, and then returns.
     * From here on out, the internal manager thread will maintian the
     * connection by continually re-establishing the connection if it ever gets
     * broken.
     *
     * @param addr The IP address to maintain a connection to
     * @param timeout_millis When connection dies, wait this many milliseconds
     *        before attempting to reestablish
     * @param service_name The name of the serivce at the other end of the
     *        connection used for descriptive log messages
     */
    void add(const sockaddr_in &addr, uint32_t timeout_millis,
             const char *service_name);

    /**
     * Same as above method except installs a dispatch handler on the connection
     *
     * @param addr The IP address to maintain a connection to
     * @param timeout_millis The timeout value (in milliseconds) that gets passed
     *        into Comm::connect and also used as the waiting period betweeen
     *        connection attempts
     * @param service_name The name of the serivce at the other end of the
     *        connection used for descriptive log messages
     * @param handler This is the default handler to install on the connection.
     *        All events get changed through to this handler.
     */
    void add(const sockaddr_in &addr, uint32_t timeout_millis,
             const char *service_name, DispatchHandlerPtr &handler);

    /**
     * Adds a connection to the connection manager with a specific local
     * address.  The address structure addr holds an address that the
     * connection manager should maintain a connection to.  This method first
     * checks to see if the address is already registered with the connection
     * manager and returns immediately if it is.  Otherwise, it adds the
     * address to an internal connection map, attempts to establish a
     * connection to the address, and then returns.  From here on out, the
     * internal manager thread will maintian the connection by continually
     * re-establishing the connection if it ever gets broken.
     *
     * @param addr The IP address to maintain a connection to
     * @param local_addr The local address to bind to
     * @param timeout_millis When connection dies, wait this many 
     *        milliseconds before attempting to reestablish
     * @param service_name The name of the serivce at the other end of the
     *        connection used for descriptive log messages
     */
    void add(const sockaddr_in &addr, const sockaddr_in &local_addr,
             uint32_t timeout_millis, const char *service_name);

    /**
     * Same as above method except installs a dispatch handler on the connection
     *
     * @param addr The IP address to maintain a connection to
     * @param local_addr The local address to bind to
     * @param timeout_millis The timeout value (in milliseconds) that gets passed
     *        into Comm::connect and also used as the waiting period betweeen
     *        connection attempts
     * @param service_name The name of the serivce at the other end of the
     *        connection used for descriptive log messages
     * @param handler This is the default handler to install on the connection.
     *        All events get changed through to this handler.
     */
    void add(const sockaddr_in &addr, const sockaddr_in &local_addr,
             uint32_t timeout_millis, const char *service_name,
             DispatchHandlerPtr &handler);

    /**
     * Removes a connection from the connection manager
     *
     * @param addr remote address of connection to remove
     * @return Error code (Error::OK on success)
     */
    int remove(struct sockaddr_in &addr);

    /**
     * This method blocks until the connection to the given address is
     * established.  The given address must have been previously added with a
     * call to Add.  If the connection is not established within max_wait_millis,
     * then the method returns false.
     *
     * @param addr the address of the connection to wait for
     * @param max_wait_millis The maximum time to wait for the connection before
     *        returning
     * @return true if connected, false otherwise
     */
    bool wait_for_connection(const sockaddr_in &addr, uint32_t max_wait_millis);

    /**
     * This method blocks until the connection to the given address is
     * established.  The given address must have been previously added with a
     * call to Add.  If the connection is not established before the timer
     * expires, then the method returns false.
     *
     * @param addr the address of the connection to wait for
     * @param timer running timer object
     * @return true if connected, false otherwise
     */
    bool wait_for_connection(const sockaddr_in &addr, Timer &timer);

    /**
     * Returns the Comm object associated with this connection manager
     *
     * @return the assocated comm object
     */
    Comm *get_comm() { return m_impl->comm; }

    /**
     * This method sets a 'quiet_mode' flag which can disable the generation
     * of log messages upon failed connection attempts.  It is set to false
     * by default.
     *
     * @param mode The new value for the quiet_mode flag
     */
    void set_quiet_mode(bool mode) { m_impl->quiet_mode = mode; }

    /**
     * This is the comm layer dispatch callback method.  It should only get
     * called by the AsyncComm subsystem.
     */
    virtual void handle(EventPtr &event);

    /**
     * This is the Boost thread "run" method.  It is called by the manager
     * thread when it starts up.
     */
    void operator()();

  private:

    void send_connect_request(ConnectionState *conn_state);

    SharedImpl *m_impl;

  };
  typedef boost::intrusive_ptr<ConnectionManager> ConnectionManagerPtr;

}


#endif // HYPERTABLE_CONNECTIONMANAGER_H
