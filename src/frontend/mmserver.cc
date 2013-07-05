/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sstream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pwd.h>
#include <typeinfo>
#include <signal.h>
#ifdef HAVE_UTEMPTER
#include <utempter.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/stat.h>

#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#if HAVE_PTY_H
#include <pty.h>
#elif HAVE_UTIL_H
#include <util.h>
#endif

#if FORKPTY_IN_LIBUTIL
#include <libutil.h>
#endif

#include "completeterminal.h"
#include "swrite.h"
#include "user.h"
#include "fatal_assert.h"
#include "locale_utils.h"
#include "pty_compat.h"
#include "select.h"
#include "timestamp.h"
#include "fatal_assert.h"
#include "commandstream.h"

#ifndef _PATH_BSHELL
#define _PATH_BSHELL "/bin/sh"
#endif

#include "networktransport.cc"

void serve(Term::CommandStream &terminal,
  Network::Transport<Term::CommandStream, Term::CommandStream> &network);

int run_server( const char *desired_ip, const char *desired_port,
                const string &command_path, char *command_argv[],
                const int colors, bool verbose, bool with_motd );

using namespace std;

void print_usage( const char *argv0 )
{
  fprintf( stderr, "Usage: %s new [-s] [-v] [-i LOCALADDR] [-p PORT[:PORT2]] [-c COLORS] [-l NAME=VALUE] [-- COMMAND...]\n", argv0 );
}

void print_motd( void );
void chdir_homedir( void );
bool motd_hushed( void );
void warn_unattached( const string & ignore_entry );

/* Simple spinloop */
void spin( void )
{
  static unsigned int spincount = 0;
  spincount++;

  if ( spincount > 10 ) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 100000000; /* 0.1 sec */
    nanosleep( &req, NULL );
    freeze_timestamp();
  }
}

string get_SSH_IP( void )
{
  const char *SSH_CONNECTION = getenv( "SSH_CONNECTION" );
  if ( !SSH_CONNECTION ) { /* Older sshds don't set this */
    fprintf( stderr, "Warning: SSH_CONNECTION not found; binding to any interface.\n" );
    return string( "0.0.0.0" );
  }
  istringstream ss( SSH_CONNECTION );
  string dummy, local_interface_IP;
  ss >> dummy >> dummy >> local_interface_IP;
  if ( !ss ) {
    fprintf( stderr, "Warning: Could not parse SSH_CONNECTION; binding to any interface.\n" );
    return string( "0.0.0.0" );
  }

  /* Strip IPv6 prefix. */
  const char IPv6_prefix[] = "::ffff:";

  if ( ( local_interface_IP.length() > strlen( IPv6_prefix ) )
       && ( 0 == strncasecmp( local_interface_IP.c_str(), IPv6_prefix, strlen( IPv6_prefix ) ) ) ) {
    return local_interface_IP.substr( strlen( IPv6_prefix ) );
  }

  return local_interface_IP;
}

int main( int argc, char *argv[] )
{
  /* For security, make sure we don't dump core */
  Crypto::disable_dumping_core();

  /* Detect edge case */
  fatal_assert( argc > 0 );

  const char *desired_ip = NULL;
  string desired_ip_str;
  const char *desired_port = NULL;
  string command_path;
  char **command_argv = NULL;
  int colors = 0;
  bool verbose = false; /* don't close stdin/stdout/stderr */
  /* Will cause mosh-server not to correctly detach on old versions of sshd. */
  list<string> locale_vars;

  /* strip off command */
  for ( int i = 0; i < argc; i++ ) {
    if ( 0 == strcmp( argv[ i ], "--" ) ) { /* -- is mandatory */
      if ( i != argc - 1 ) {
        command_argv = argv + i + 1;
      }
      argc = i; /* rest of options before -- */
      break;
    }
  }

  /* Parse new command-line syntax */
  if ( (argc >= 2)
       && (strcmp( argv[ 1 ], "new" ) == 0) ) {
    /* new option syntax */
    int opt;
    while ( (opt = getopt( argc - 1, argv + 1, "i:p:c:svl:" )) != -1 ) {
      switch ( opt ) {
      case 'i':
        desired_ip = optarg;
        break;
      case 'p':
        desired_port = optarg;
        break;
      case 's':
        desired_ip_str = get_SSH_IP();
        desired_ip = desired_ip_str.c_str();
        fatal_assert( desired_ip );
        break;
      case 'c':
        colors = myatoi( optarg );
        break;
      case 'v':
        verbose = true;
        break;
      case 'l':
        locale_vars.push_back( string( optarg ) );
        break;
      default:
        print_usage( argv[ 0 ] );
        /* don't die on unknown options */
      }
    }
  } else if ( argc == 1 ) {
    /* legacy argument parsing for older client wrapper script */
    /* do nothing */
  } else if ( argc == 2 ) {
    desired_ip = argv[ 1 ];
  } else if ( argc == 3 ) {
    desired_ip = argv[ 1 ];
    desired_port = argv[ 2 ];
  } else {
    print_usage( argv[ 0 ] );
    exit( 1 );
  }

  /* Sanity-check arguments */
  if ( desired_ip
       && ( strspn( desired_ip, "0123456789." ) != strlen( desired_ip ) ) ) {
    fprintf( stderr, "%s: Bad IP address (%s)\n", argv[ 0 ], desired_ip );
    print_usage( argv[ 0 ] );
    exit( 1 );
  }

  int dpl, dph;
  if ( desired_port && ! Connection::parse_portrange( desired_port, dpl, dph ) ) {
    fprintf( stderr, "%s: Bad UDP port range (%s)\n", argv[ 0 ], desired_port );
    print_usage( argv[ 0 ] );
    exit( 1 );
  }

  bool with_motd = false;

  try {
    return run_server( desired_ip, desired_port, command_path, command_argv, colors, verbose, with_motd );
  } catch ( const Network::NetworkException& e ) {
    fprintf( stderr, "Network exception: %s: %s\n",
             e.function.c_str(), strerror( e.the_errno ) );
    return 1;
  } catch ( const Crypto::CryptoException& e ) {
    fprintf( stderr, "Crypto exception: %s\n",
             e.text.c_str() );
    return 1;
  }
}

int run_server( const char *desired_ip, const char *desired_port,
                const string &command_path, char *command_argv[],
                const int colors, bool verbose, bool with_motd ) {
  Term::CommandStream blank_stream;
  Term::CommandStream blank_terminal;
  Network::Transport<Term::CommandStream, Term::CommandStream> *network =
    new Network::Transport<Term::CommandStream, Term::CommandStream>(
      blank_terminal, blank_stream, desired_ip, desired_port
    );

  if ( verbose ) {
    network->set_verbose();
  }

  printf( "\nMOSH CONNECT %d %s\n", network->port(), network->get_key().c_str() );
  fflush( stdout );

  /* don't let signals kill us */
  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = 0;
  fatal_assert( 0 == sigfillset( &sa.sa_mask ) );
  fatal_assert( 0 == sigaction( SIGHUP, &sa, NULL ) );
  fatal_assert( 0 == sigaction( SIGPIPE, &sa, NULL ) );

  fprintf( stderr, "\nmosh-server (%s)\n", PACKAGE_STRING );
  fprintf( stderr, "Copyright 2012 Keith Winstein <mosh-devel@mit.edu>\n" );
  fprintf( stderr, "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\nThis is free software: you are free to change and redistribute it.\nThere is NO WARRANTY, to the extent permitted by law.\n\n" );

  fprintf( stderr, "[mosh-server detached, pid = %d]\n", (int)getpid() );

  try {
    serve( blank_terminal, *network );
  } catch ( const Network::NetworkException& e ) {
    fprintf( stderr, "Network exception: %s: %s\n",
             e.function.c_str(), strerror( e.the_errno ) );
  } catch ( const Crypto::CryptoException& e ) {
    fprintf( stderr, "Crypto exception: %s\n",
             e.text.c_str() );
  }

  delete network;

  printf( "\n[mosh-server is exiting.]\n" );

  return 0;
}

void serve(Term::CommandStream &terminal,
  Network::Transport<Term::CommandStream, Term::CommandStream> &network)
{
  /* prepare to poll for events */
  Select &sel = Select::get_instance();
  sel.add_signal( SIGTERM );
  sel.add_signal( SIGINT );

  uint64_t last_remote_num = network.get_remote_state_num();

  #ifdef HAVE_UTEMPTER
  bool connected_utmp = false;

  struct in_addr saved_addr;
  saved_addr.s_addr = 0;
  #endif

  while ( 1 ) {
    try {
      uint64_t now = Network::timestamp();

      const int timeout_if_no_client = 60000;
      int timeout = network.wait_time();
      if ( (!network.get_remote_state_num())
           || network.shutdown_in_progress() ) {
        timeout = min( timeout, 5000 );
      }

      /* poll for events */
      sel.clear_fds();
      std::vector< int > fd_list( network.fds() );
      assert( fd_list.size() == 1 ); /* servers don't hop */
      int network_fd = fd_list.back();
      sel.add_fd( network_fd );

      int active_fds = sel.select( timeout );
      if ( active_fds < 0 ) {
        perror( "select" );
        break;
      }

      now = Network::timestamp();
      uint64_t time_since_remote_state = now - network.get_latest_remote_state().timestamp;

      if ( sel.read( network_fd ) ) {
        /* packet received from the network */
        network.recv();
        
        /* is new user input available for the terminal? */
        if ( network.get_remote_state_num() != last_remote_num ) {
          last_remote_num = network.get_remote_state_num();
          
          Term::CommandStream command_stream;
          command_stream.apply_string(network.get_remote_diff());
          for (size_t i = 0; i < command_stream.size(); i++) {
            const string *action = command_stream.get_action(i);
            terminal.push_back(*action);
          }

          /* update client with new state of terminal */
          if ( !network.shutdown_in_progress() ) {
            network.set_current_state( terminal );
          }
        }
      }

      if ( sel.any_signal() ) {
        /* shutdown signal */
        if ( network.has_remote_addr() && (!network.shutdown_in_progress()) ) {
          network.start_shutdown();
        } else {
          break;
        }
      }
      
      if ( sel.error( network_fd ) ) {
        /* network problem */
        break;
      }

      /* quit if our shutdown has been acknowledged */
      if ( network.shutdown_in_progress() && network.shutdown_acknowledged() ) {
        break;
      }

      /* quit after shutdown acknowledgement timeout */
      if ( network.shutdown_in_progress() && network.shutdown_ack_timed_out() ) {
        break;
      }

      /* quit if we received and acknowledged a shutdown request */
      if ( network.counterparty_shutdown_ack_sent() ) {
        break;
      }

      if ( !network.get_remote_state_num()
           && time_since_remote_state >= uint64_t( timeout_if_no_client ) ) {
        fprintf( stderr, "No connection within %d seconds.\n",
                 timeout_if_no_client / 1000 );
        break;
      }

      network.tick();
    } catch ( const Network::NetworkException& e ) {
      fprintf( stderr, "%s: %s\n", e.function.c_str(), strerror( e.the_errno ) );
      spin();
    } catch ( const Crypto::CryptoException& e ) {
      if ( e.fatal ) {
        throw;
      } else {
        fprintf( stderr, "Crypto exception: %s\n", e.text.c_str() );
      }
    }
  }
}

/* OpenSSH prints the motd on startup, so we will too */
void print_motd( void )
{
  FILE *motd = fopen( "/etc/motd", "r" );
  if ( !motd ) {
    return; /* don't report error on missing or forbidden motd */
  }

  const int BUFSIZE = 256;

  char buffer[ BUFSIZE ];
  while ( 1 ) {
    size_t bytes_read = fread( buffer, 1, BUFSIZE, motd );
    if ( bytes_read == 0 ) {
      break; /* don't report error */
    }
    size_t bytes_written = fwrite( buffer, 1, bytes_read, stdout );
    if ( bytes_written == 0 ) {
      break;
    }
  }

  fclose( motd );
}

void chdir_homedir( void )
{
  struct passwd *pw = getpwuid( geteuid() );
  if ( pw == NULL ) {
    perror( "getpwuid" );
    return; /* non-fatal */
  }

  if ( chdir( pw->pw_dir ) < 0 ) {
    perror( "chdir" );
  }

  if ( setenv( "PWD", pw->pw_dir, 1 ) < 0 ) {
    perror( "setenv" );
  }
}

bool motd_hushed( void )
{
  /* must be in home directory already */
  struct stat buf;
  return (0 == lstat( ".hushlogin", &buf ));
}

bool device_exists( const char *ut_line )
{
  string device_name = string( "/dev/" ) + string( ut_line );
  struct stat buf;
  return (0 == lstat( device_name.c_str(), &buf ));
}

string mosh_read_line( FILE *file )
{
  string ret;
  while ( !feof( file ) ) {
    char next = getc( file );
    if ( next == '\n' ) {
      return ret;
    }
    ret.push_back( next );
  }
  return ret;
}

void warn_unattached( const string & ignore_entry )
{
#ifdef HAVE_UTMPX_H
  /* get username */
  const struct passwd *pw = getpwuid( geteuid() );
  if ( pw == NULL ) {
    perror( "getpwuid" );
    /* non-fatal */
    return;
  }

  const string username( pw->pw_name );

  /* look for unattached sessions */
  vector< string > unattached_mosh_servers;

  while ( struct utmpx *entry = getutxent() ) {
    if ( (entry->ut_type == USER_PROCESS)
         && (username == string( entry->ut_user )) ) {
      /* does line show unattached mosh session */
      string text( entry->ut_host );
      if ( (text.size() >= 5)
           && (text.substr( 0, 5 ) == "mosh ")
           && (text[ text.size() - 1 ] == ']')
           && (text != ignore_entry)
           && device_exists( entry->ut_line ) ) {
        unattached_mosh_servers.push_back( text );
      }
    }
  }

  /* print out warning if necessary */
  if ( unattached_mosh_servers.empty() ) {
    return;
  } else if ( unattached_mosh_servers.size() == 1 ) {
    printf( "\033[37;44mMosh: You have a detached Mosh session on this server (%s).\033[m\n\n",
            unattached_mosh_servers.front().c_str() );
  } else {
    string pid_string;

    for ( vector< string >::const_iterator it = unattached_mosh_servers.begin();
          it != unattached_mosh_servers.end();
          it++ ) {
      pid_string += "        - " + *it + "\n";
    }

    printf( "\033[37;44mMosh: You have %d detached Mosh sessions on this server, with PIDs:\n%s\033[m\n",
            (int)unattached_mosh_servers.size(), pid_string.c_str() );
  }
#endif /* HAVE_UTMPX_H */
}
