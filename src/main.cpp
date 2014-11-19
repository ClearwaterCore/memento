/**
 * @file main.cpp main function for memento
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include <getopt.h>
#include <signal.h>
#include <semaphore.h>
#include <strings.h>

#include "memcachedstore.h"
#include "httpstack.h"
#include "homesteadconnection.h"
#include "log.h"
#include "logger.h"
#include "saslogger.h"
#include "handlers.h"
#include "sas.h"
#include "load_monitor.h"
#include "authstore.h"
#include "mementosaslogger.h"

struct options
{
  std::string local_host;
  std::string http_address;
  unsigned short http_port;
  int http_threads;
  int http_worker_threads;
  std::string homestead_http_name;
  int digest_timeout;
  std::string home_domain;
  std::string sas_server;
  std::string sas_system_name;
  bool access_log_enabled;
  std::string access_log_directory;
  bool log_to_file;
  std::string log_directory;
  int log_level;
};

// Enum for option types not assigned short-forms
enum OptionTypes
{
  LOCAL_HOST = 128, // start after the ASCII set ends to avoid conflicts
  HTTP_ADDRESS,
  HTTP_THREADS,
  HTTP_WORKER_THREADS,
  HOMESTEAD_HTTP_NAME,
  DIGEST_TIMEOUT,
  HOME_DOMAIN,
  SAS_CONFIG,
  ACCESS_LOG,
  LOG_FILE,
  LOG_LEVEL,
  HELP
};

const static struct option long_opt[] =
{
  {"localhost",           required_argument, NULL, LOCAL_HOST},
  {"http",                required_argument, NULL, HTTP_ADDRESS},
  {"http-threads",        required_argument, NULL, HTTP_THREADS},
  {"http-worker-threads", required_argument, NULL, HTTP_WORKER_THREADS},
  {"homestead-http-name", required_argument, NULL, HOMESTEAD_HTTP_NAME},
  {"digest-timeout",      required_argument, NULL, DIGEST_TIMEOUT},
  {"home-domain",         required_argument, NULL, HOME_DOMAIN},
  {"sas",                 required_argument, NULL, SAS_CONFIG},
  {"access-log",          required_argument, NULL, ACCESS_LOG},
  {"log-file",            required_argument, NULL, LOG_FILE},
  {"log-level",           required_argument, NULL, LOG_LEVEL},
  {"help",                no_argument,       NULL, HELP},
  {NULL,                  0,                 NULL, 0},
};

void usage(void)
{
  puts("Options:\n"
       "\n"
       " --localhost <hostname>     Specify the local hostname or IP address\n"
       " --http <address>[:<port>]\n"
       "              Set HTTP bind address and port (default: 0.0.0.0:11888)\n"
       " --http-threads N           Number of HTTP threads (default: 1)\n"
       " --http-worker-threads N    Number of HTTP worker threads (default: 50)\n"
       " --homestead-http-name <name>\n"
       "                            Set HTTP address to contact Homestead\n"
       " --digest-timeout N         Time a digest is stored in memcached (in seconds)\n"
       " --home-domain <domain>     The home domain of the deployment\n"
       " --sas <host>,<system name>\n"
       "    Use specified host as Service Assurance Server and specified\n"
       "    system name to identify this system to SAS. If this option isn't\n"
       "    specified, SAS is disabled\n"
       " --access-log <directory>\n"
       "                            Generate access logs in specified directory\n"
       " --log-file <directory>\n"
       "                            Log to file in specified directory\n"
       " --log-level N              Set log level to N (default: 4)\n"
       " --help                     Show this help screen\n");
}

int init_logging_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  while ((opt = getopt_long(argc, argv, "", long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case LOG_FILE:
      options.log_to_file = true;
      options.log_directory = std::string(optarg);
      break;

    case LOG_LEVEL:
      options.log_level = atoi(optarg);
      break;

    default:
      // Ignore other options at this point
      break;
    }
  }

  return 0;
}

int init_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  while ((opt = getopt_long(argc, argv, "", long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case LOCAL_HOST:
      LOG_INFO("Local host: %s", optarg);
      options.local_host = std::string(optarg);
      break;

    case HTTP_ADDRESS:
      LOG_INFO("HTTP bind address: %s", optarg);
      options.http_address = std::string(optarg);
      break;

    case HTTP_THREADS:
      LOG_INFO("Number of HTTP threads: %s", optarg);
      options.http_threads = atoi(optarg);
      break;

    case HTTP_WORKER_THREADS:
      LOG_INFO("Number of HTTP worker threads: %s", optarg);
      options.http_worker_threads = atoi(optarg);
      break;

    case HOMESTEAD_HTTP_NAME:
      LOG_INFO("Homestead HTTP address: %s", optarg);
      options.homestead_http_name = std::string(optarg);
      break;

    case DIGEST_TIMEOUT:
      options.digest_timeout = atoi(optarg);

      if (options.digest_timeout == 0)
      {
        // If the supplied option is invalid then revert to the
        // default five minutes
        options.digest_timeout = 300;
      }

      LOG_INFO("Digest timeout: %s", optarg);
      break;

    case HOME_DOMAIN:
      options.home_domain = std::string(optarg);
      LOG_INFO("Home domain: %s", optarg);
      break;

    case SAS_CONFIG:
    {
      std::vector<std::string> sas_options;
      Utils::split_string(std::string(optarg), ',', sas_options, 0, false);

      if ((sas_options.size() == 2) &&
          !sas_options[0].empty() &&
          !sas_options[1].empty())
      {
        options.sas_server = sas_options[0];
        options.sas_system_name = sas_options[1];
        LOG_INFO("SAS set to %s\n", options.sas_server.c_str());
        LOG_INFO("System name is set to %s\n", options.sas_system_name.c_str());
      }
      else
      {
        LOG_INFO("Invalid --sas option, SAS disabled\n");
      }
    }
    break;

    case ACCESS_LOG:
      LOG_INFO("Access log: %s", optarg);
      options.access_log_enabled = true;
      options.access_log_directory = std::string(optarg);
      break;

    case LOG_FILE:
    case LOG_LEVEL:
      // Ignore these options - they're handled by init_logging_options
      break;

    case HELP:
      usage();
      return -1;

    default:
      LOG_ERROR("Unknown option. Run with --help for options.\n");
      return -1;
    }
  }

  return 0;
}

static sem_t term_sem;

// Signal handler that triggers memento termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

// Signal handler that simply dumps the stack and then crashes out.
void exception_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);

  // Log the signal, along with a backtrace.
  LOG_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  LOG_COMMIT();

  // Dump a core.
  abort();
}

int main(int argc, char**argv)
{
  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, exception_handler);
  signal(SIGSEGV, exception_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  struct options options;
  options.local_host = "127.0.0.1";
  options.http_address = "0.0.0.0";
  options.http_port = 11888;
  options.http_threads = 1;
  options.http_worker_threads = 50;
  options.homestead_http_name = "homestead-http-name.unknown";
  options.digest_timeout = 300;
  options.home_domain = "home.domain";
  options.sas_server = "0.0.0.0";
  options.sas_system_name = "";
  options.access_log_enabled = false;
  options.log_to_file = false;
  options.log_level = 0;

  if (init_logging_options(argc, argv, options) != 0)
  {
    return 1;
  }

  Log::setLoggingLevel(options.log_level);

  if ((options.log_to_file) && (options.log_directory != ""))
  {
    // Work out the program name from argv[0], stripping anything before the final slash.
    char* prog_name = argv[0];
    char* slash_ptr = rindex(argv[0], '/');

    if (slash_ptr != NULL)
    {
      prog_name = slash_ptr + 1;
    }

    Log::setLogger(new Logger(options.log_directory, prog_name));
  }

  LOG_STATUS("Log level set to %d", options.log_level);

  std::stringstream options_ss;

  for (int ii = 0; ii < argc; ii++)
  {
    options_ss << argv[ii];
    options_ss << " ";
  }

  std::string options_str = "Command-line options were: " + options_ss.str();

  LOG_INFO(options_str.c_str());

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  AccessLogger* access_logger = NULL;

  if (options.access_log_enabled)
  {
    LOG_STATUS("Access logging enabled to %s", options.access_log_directory.c_str());
    access_logger = new AccessLogger(options.access_log_directory);
  }

  SAS::init(options.sas_system_name,
            "memento",
            SASEvent::CURRENT_RESOURCE_BUNDLE,
            options.sas_server,
            sas_write);

  // Ensure our random numbers are unpredictable.
  unsigned int seed;
  seed = time(NULL) ^ getpid();
  srand(seed);

  MemcachedStore* m_store = new MemcachedStore(false, 
                                               "./cluster_settings",
                                               NULL,
                                               NULL);
  AuthStore* auth_store = new AuthStore(m_store, options.digest_timeout);

  LoadMonitor* load_monitor = new LoadMonitor(100000, // Initial target latency (us)
                                              20, // Maximum token bucket size.
                                              10.0, // Initial token fill rate (per sec).
                                              10.0); // Minimum token fill rate (pre sec).

  // Create a DNS resolver and an HTTP specific resolver.
  int af = AF_INET;
  struct in6_addr dummy_addr;
  if (inet_pton(AF_INET6, options.local_host.c_str(), &dummy_addr) == 1)
  {
    LOG_DEBUG("Local host is an IPv6 address");
    af = AF_INET6;
  }

  DnsCachedResolver* dns_resolver = new DnsCachedResolver("127.0.0.1");
  HttpResolver* http_resolver = new HttpResolver(dns_resolver, af);
  HomesteadConnection* homestead_conn =
    new HomesteadConnection(options.homestead_http_name, http_resolver);

  // Create and start the call list store.
  CallListStore::Store* call_list_store = new CallListStore::Store();
  call_list_store->initialize();
  call_list_store->configure("localhost", 9160);
  CassandraStore::ResultCode store_rc = call_list_store->start();

  if (store_rc != CassandraStore::OK)
  {
    LOG_ERROR("Unable to create call list store (RC = %d)", store_rc);
    exit(3);
  }

  HttpStack* http_stack = HttpStack::get_instance();

  CallListTask::Config call_list_config(auth_store, homestead_conn, call_list_store, options.home_domain);

  MementoSasLogger sas_logger;
  HttpStackUtils::PingHandler ping_handler;
  HttpStackUtils::SpawningHandler<CallListTask, CallListTask::Config> call_list_handler(&call_list_config, &sas_logger);
  HttpStackUtils::HandlerThreadPool pool(options.http_worker_threads);

  try
  {
    http_stack->initialize();
    http_stack->configure(options.http_address,
                          options.http_port,
                          options.http_threads,
                          access_logger,
                          load_monitor);
    http_stack->register_handler("^/ping$", &ping_handler);
    http_stack->register_handler("^/org.projectclearwater.call-list/users/[^/]*/call-list.xml$",
                                    pool.wrap(&call_list_handler));
    http_stack->start();
  }
  catch (HttpStack::Exception& e)
  {
    LOG_ERROR("Failed to initialize HttpStack stack - function %s, rc %d", e._func, e._rc);
    exit(2);
  }

  LOG_STATUS("Start-up complete - wait for termination signal");
  sem_wait(&term_sem);
  LOG_STATUS("Termination signal received - terminating");

  try
  {
    http_stack->stop();
    http_stack->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    LOG_ERROR("Failed to stop HttpStack stack - function %s, rc %d", e._func, e._rc);
  }

  call_list_store->stop();
  call_list_store->wait_stopped();

  delete homestead_conn; homestead_conn = NULL;
  delete call_list_store; call_list_store = NULL;
  delete http_resolver; http_resolver = NULL;
  delete dns_resolver; dns_resolver = NULL;
  delete load_monitor; load_monitor = NULL;
  delete auth_store; auth_store = NULL;
  delete call_list_store; call_list_store = NULL;
  delete m_store; m_store = NULL;

  SAS::term();

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
