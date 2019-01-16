/*
 * Copyright (c) 2018-present StackPath, LLC
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#define _GNU_SOURCE // for GNU basename()

#include "cpu.h"     // for get_online_cpu_set(), parse_cpu_list(),
                     //     parse_cpu_mask()
#include "manager.h" // for manager(), manager_arguments

#include <getopt.h>   // for getopt_long(), optarg, optind, option, optopt
#include <inttypes.h> // for strtoumax()
#include <sched.h>    // for CPU_CLR(), CPU_COUNT(), CPU_ISSET(), CPU_SET(),
                      //     cpu_set_t
#include <stdbool.h>  // for bool, false, true
#include <stdio.h>    // for fprintf(), fputs(), printf(), puts(), sprintf(),
                      //     stderr
#include <stdlib.h>   // for malloc()
#include <string.h>   // for GNU basename(), memset(), strcmp(), strlen()
#include <unistd.h>   // for _SC_NPROCESSORS_CONF, sysconf()

#define EXIT_OK          0
#define EXIT_FAIL        1
#define EXIT_FAIL_OPTION 2

#define MAX_SHORT_BADOPT_LENGTH 2
#define OPTION_COUNT_BASE 10
#define OPTION_CPU_LIST_BASE 10

char *program_basename = NULL;

static const struct option long_options[] = {
  {"count",           required_argument, NULL, 'c'},
  {"direction",       required_argument, NULL, 'd'},
  {"help",            no_argument,       NULL, 'h'},
  {"cpu-list",        required_argument, NULL, 'l'},
  {"cpu-mask",        required_argument, NULL, 'm'},
  {"promiscuous",     no_argument,       NULL, 'p'},
  {"packet-buffered", no_argument,       NULL, 'U'},
  {"verbose",         no_argument,       NULL, 'v'},
  {"version",         no_argument,       NULL, 'V'},
  {"write",           required_argument, NULL, 'w'},
  {0, 0, NULL, 0}
};

static void usage(void) {
  puts("Usage:");
  printf("  %s [OPTIONS] [INTERFACE]\n\n", program_basename);

  puts("Options:\n"
       "  -c, [--count=N]              # Exit after receiving N packets.\n"
       "  -d, [--direction=DIRECTION]  # Capture only packets matching DIRECTION.\n"
       "                               #   DIRECTION can be 'rx', 'tx', or 'rxtx'.\n"
       "                               #   Default matches invocation (i.e. DIRECTION\n"
       "                               #   defaults to 'rx' when invocation is 'rxcpu',\n"
       "                               #   'tx' when 'txcpu', and 'rxtx' when\n"
       "                               #   'rxtxcpu').\n"
       "  -h, [--help]                 # Display this help and exit.\n"
       "  -l, [--cpu-list=CPULIST]     # Capture only on cpus in CPULIST (e.g. if\n"
       "                               #   CPULIST is '0,2-4,6', only packets on cpus\n"
       "                               #   0, 2, 3, 4, and 6 will be captured).\n"
       "  -m, [--cpu-mask=CPUMASK]     # Capture only on cpus in CPUMASK (e.g. if\n"
       "                               #   CPUMASK is '5d', only packets on cpus 0, 2,\n"
       "                               #   3, 4, and 6 will be captured).\n"
       "  -p, [--promiscuous]          # Put the interface into promiscuous mode.\n"
       "  -U, [--packet-buffered]      # When writing to a pcap file, the write buffer\n"
       "                               #   will be flushed just after each packet is\n"
       "                               #   placed in it.\n"
       "  -v, [--verbose]              # Display more verbose output.\n"
       "  -V, [--version]              # Display the version and exit.\n"
       "  -w, [--write=FILE]           # Write packets to FILE in pcap format. FILE is\n"
       "                               #   used as a template for per-cpu filenames\n"
       "                               #   (e.g. if capturing on a system with 2 cpus,\n"
       "                               #   cpus 0 and 1, and FILE set to 'out.pcap',\n"
       "                               #   the cpu 0 capture will be written to\n"
       "                               #   'out-0.pcap' and the cpu 1 capture will be\n"
       "                               #   written to 'out-1.pcap'). Writing to stdout\n"
       "                               #   is supported by setting FILE to '-', but\n"
       "                               #   only when capturing on a single cpu.");
}

static void usage_short(void) {
  fprintf(stderr,
          "Usage: %s [--help] [--count=N] [--cpu-list=CPULIST|--cpu-mask=CPUMASK]\n"
          "       %*s [--direction=DIRECTION] [--packet-buffered] [--promiscuous]\n"
          "       %*s [--verbose] [--version] [--write=FILE] [INTERFACE]\n",
          program_basename,
          (int) strlen(program_basename), "",
          (int) strlen(program_basename), "");
}

int main(int argc, char **argv) {
  program_basename = basename(argv[0]);
  struct manager_arguments args;
  memset(&args, 0, sizeof(args));

  args.program_fullname = argv[0];
  args.program_basename = program_basename;

  /*
   * capture_rx and capture_tx defaults are based on the invocation.
   */
  args.capture_rx = true;
  args.capture_tx = true;

  if (strcmp(program_basename, "rxcpu") == 0) {
    args.capture_tx = false;
  }

  if (strcmp(program_basename, "txcpu") == 0) {
    args.capture_rx = false;
  }

  int c;
  char *badopt = NULL;
  char *cpu_list = NULL;
  char *cpu_mask = NULL;
  char *endptr = NULL;
  bool help = false;

  /*
   * optstring must start with ":" so ':' is returned for a missing option
   * argument. Otherwise '?' is returned for both invalid option and missing
   * option argument.
   */
  while ((c = getopt_long(argc, argv, ":c:d:hl:m:pUvVw:", long_options, 0)) != -1) {
    switch (c) {
      case 'c':
        args.packet_count = strtoumax(optarg, &endptr, OPTION_COUNT_BASE);
        if (*endptr) {
          fprintf(stderr, "%s: Invalid count '%s'.\n", program_basename, optarg);
          usage_short();
          return EXIT_FAIL_OPTION;
        }
        break;

      case 'd':
        if (strcmp(optarg, "rx") == 0) {
          args.capture_rx = true;
          args.capture_tx = false;
        } else if (strcmp(optarg, "tx") == 0) {
          args.capture_rx = false;
          args.capture_tx = true;
        } else if (strcmp(optarg, "rxtx") == 0) {
          args.capture_rx = true;
          args.capture_tx = true;
        } else {
          fprintf(stderr, "%s: Invalid direction '%s'.\n", program_basename, optarg);
          usage_short();
          return EXIT_FAIL_OPTION;
        }
        break;

      case 'h':
        help = true;
        break;

      case 'l':
        cpu_list = optarg;
        if (parse_cpu_list(optarg, &(args.capture_cpu_set))) {
          fprintf(stderr, "%s: Invalid cpu list '%s'.\n", program_basename, optarg);
          usage_short();
          return EXIT_FAIL_OPTION;
        }
        break;

      case 'm':
        cpu_mask = optarg;
        if (parse_cpu_mask(optarg, &(args.capture_cpu_set))) {
          fprintf(stderr, "%s: Invalid cpu mask '%s'.\n", program_basename, optarg);
          usage_short();
          return EXIT_FAIL_OPTION;
        }
        break;

      case 'p':
        args.promiscuous = true;
        break;

      case 'U':
        args.packet_buffered = true;
        break;

      case 'v':
        args.verbose = true;
        break;

      case 'V':
        printf("%s version %s\n", program_basename, RXTXCPU_VERSION);
        return EXIT_OK;

      case 'w':
        args.pcap_filename = optarg;
        break;

      case ':':  /* missing option argument */
        fprintf(stderr, "%s: Option '%s' requires an argument.\n", program_basename, argv[optind-1]);
        usage_short();
        return EXIT_FAIL_OPTION;

      case '?':  /* invalid option */
      default:   /* invalid option in optstring */
        /*
         * optopt is NULL for long options. optind is the index of the next
         * argv to be processed. It is therefore tempting to use
         * argv[optind-1] to retrieve the invalid option, be it short or long.
         *
         * This method works with long options and with non-bundled short
         * options. However, it fails under certain conditions with bundled
         * short options.
         *
         * When the first invalid option in the bundle is also the last option
         * in the bundle, argv[optind-1] works.
         *
         * When the first invalid option in the bundle is not also the last
         * option in the bundle, optind is not incremented. This makes sense
         * since the bundle argv still needs further processing.
         *
         * Relying solely on optind leaves us with two difficult to distinguish
         * possibilities.
         *   1. The last or only short option in argv[optind-1] was invalid and
         *      optind was incremented.
         *   2. Any short option other than the last in argv[optind] was
         *      invalid and optind was not incremented.
         *
         * As previously mentioned, optopt is NULL for long options. It is also
         * reliable for finding the invalid short option with one caveat; we
         * need to keep our optstring clean. If we arrive here via the default
         * case optopt will be NULL and we'll have a corner case for the
         * invalid short option mistakenly in optstring.
         *
         * As long as we keep optstring clean, we can use optopt for short
         * options and argv[optind-1] for long options.
         */
        if (optopt) {
          badopt = malloc(MAX_SHORT_BADOPT_LENGTH + 1);
          sprintf(badopt, "-%c", optopt);
        } else {
          badopt = argv[optind-1];
        }
        fprintf(stderr, "%s: Unrecognized option '%s'.\n", program_basename, badopt);
        free(badopt);
        usage_short();
        return EXIT_FAIL_OPTION;
    }
  }

  if (help) {
    usage();
    return EXIT_OK;
  }

  if (cpu_list && cpu_mask) {
    fprintf(stderr, "%s: -l [--cpu-list] and -m [--cpu-mask] are mutually exclusive.\n", program_basename);
    usage_short();
    return EXIT_FAIL_OPTION;
  }

  /*
   * We need to know how many processors are configured.
   */
  args.processor_count = sysconf(_SC_NPROCESSORS_CONF);
  if (args.processor_count <= 0) {
    fprintf(stderr, "%s: Failed to get processor count.\n", program_basename);
    return EXIT_FAIL_OPTION;
  }

  if (args.verbose) {
    fprintf(stderr, "Found '%d' processors.\n", args.processor_count);
  }

  if (CPU_COUNT(&(args.capture_cpu_set)) == 0) {
    for (int i = 0; i < args.processor_count; i++) {
      CPU_SET(i, &(args.capture_cpu_set));
    }
  }

  int worker_count = 0;
  for (int i = 0; i < args.processor_count; i++) {
    if (CPU_ISSET(i, &(args.capture_cpu_set))) {
      worker_count++;
    }
  }
  if (!worker_count) {
    fprintf(
      stderr,
      "%s: No configured cpus present in cpu %s.\n",
      program_basename,
      cpu_list ? "list" : "mask"
    );
    usage_short();
    return EXIT_FAIL_OPTION;
  }

  cpu_set_t online_cpu_set;
  if (get_online_cpu_set(&online_cpu_set) != 0) {
    fprintf(stderr, "%s: Failed to get online cpu set.\n", program_basename);
    return EXIT_FAIL_OPTION;
  }

  if (CPU_COUNT(&online_cpu_set) != args.processor_count) {
    for (int i = 0; i < args.processor_count; i++) {
      if (!CPU_ISSET(i, &online_cpu_set)) {
        CPU_CLR(i, &(args.capture_cpu_set));
        if (args.verbose) {
          fprintf(stderr, "Skipping cpu '%d' since it is offline.\n", i);
        }
      }
    }
  }

  worker_count = 0;
  for (int i = 0; i < args.processor_count; i++) {
    if (CPU_ISSET(i, &(args.capture_cpu_set))) {
      worker_count++;
    }
  }
  if (!worker_count) {
    fprintf(
      stderr,
      "%s: No online cpus present in cpu %s.\n",
      program_basename,
      cpu_list ? "list" : "mask"
    );
    usage_short();
    return EXIT_FAIL_OPTION;
  }

  if (args.pcap_filename &&
      strcmp(args.pcap_filename, "-") == 0 &&
      CPU_COUNT(&(args.capture_cpu_set)) != 1) {
    fprintf(
      stderr,
      "%s: Write file '%s' (stdout) is only permitted when capturing on a single cpu.\n",
      program_basename,
      args.pcap_filename
    );
    usage_short();
    return EXIT_FAIL_OPTION;
  }

  if ((optind + 1) < argc) {
    fprintf(stderr, "%s: Only one interface argument is allowed (got [ ", program_basename);
    for (; optind < argc; optind++) {
      fprintf(stderr, "'%s'", argv[optind]);
      if ((optind + 1) < argc)
        fputs(", ", stderr);
    }
    fputs(" ]).\n", stderr);
    usage_short();
    return EXIT_FAIL_OPTION;
  }

  if (optind == argc) {
    args.ifname = NULL;
  } else {
    args.ifname = argv[optind];
  }

  if (manager(&args) == -1) {
    return EXIT_FAIL;
  }

  return EXIT_OK;
}
