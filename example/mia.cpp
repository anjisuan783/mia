//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "config.h"

#include <unistd.h>
#include <iostream>

int Usage() {
  printf("usage: ./mia -l [log4cxx.properties] -c [mia.cfg] \n");
  return -1;
}

extern std::string g_log4cxx_config_path;
extern std::string g_mia_config_path;

int ParseArgs(int argc, char* argv[]) {
  if (argc < 5) {
    return -1;
  }
  std::string audio_file_path;
  std::string video_file_path;
  int c;
  while ((c = getopt(argc, argv, "l:c:v:a:p:")) != -1) {
		switch (c) {
  		case 'l':
        g_log4cxx_config_path = optarg;
        break;
      case 'c':
        g_mia_config_path = optarg;
        break;
      default:
        ;
		}
	}
  
  return 0;
}

int ServiceStart() {
  return 0;
}

void ServiceStop() {
}
