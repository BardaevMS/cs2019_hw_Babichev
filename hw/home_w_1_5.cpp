#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <csignal>
#include <sys/stat.h>
#include <syslog.h>
#include <ctime>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#define EXIT_SUCCESS    0
#define EXIT_FAILURE    1
#define EXIT_STOP       2

//#define DEBUG

const char* mycrontab_path = "/Users/maximbardaev/Desktop/mycron/mycrontab.txt";
const char* output_path = "/Users/maximbardaev/Desktop/mycron/cron_out.txt";
const char* log_path = "/Users/maximbardaev/Desktop/mycron/cron_log.txt";
const char* beep = "beep";

const int time_wildcard = -1;

void handle_signal(int signal_id) {
  switch(signal_id) {
    case SIGQUIT:
    case SIGTERM:
    case SIGABRT:
      // Abort
      exit(EXIT_STOP);
      break;

    case SIGHUP:
      // TODO:
      break;

    default: break;
  }
}

void set_signal_handler() {
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, handle_signal);
  signal(SIGTERM, handle_signal);
  signal(SIGABRT, handle_signal);
  signal(SIGQUIT, handle_signal);
}

void launch_daemon() {
  // Return code
  int rc = 0;
  pid_t pid;
  pid = fork();

  if (pid < 0) {
    exit(EXIT_FAILURE);
  }

  // Terminate the parent process
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  // Set the child process session leader
  rc = setsid();
  if (rc < 0) {
    exit(EXIT_FAILURE);
  }

  // Handle signals
  signal(SIGHUP, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  // Fork off second time to ensure that daemon can never re-acquire a terminal again
  // and that daemon process is parented to PID 1
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }

  // Terminating parents
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  // Set file permissions
  umask(0);

  // Set working directory to root
  chdir("/");

  // Closing all open file descriptors ensuring that
  // no accidentally passed file descriptor stays
  // around in the daemon
  for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
    close(fd);
  }
}

// Prints log to <log_path>
void print_log(const char* message) {
  FILE* fd;
  if ((fd = fopen(log_path, "a+")) == NULL) {
    printf("[Error] Couldn't open/create file: %s\n", log_path);
    exit(EXIT_FAILURE);
  }

  fwrite(message, sizeof(char), strlen(message), fd);
  fclose(fd);
}

struct Job {
  int sec;
  int min;
  int hrs;

  std::vector<std::string> args;
  std::string command;
};

enum class state {
  hrs,
  min,
  sec,
  cmd,
  args
};

state get_next_state(state current_state) {
  switch(current_state) {
    case state::hrs:    return state::min;
    case state::min:    return state::sec;
    case state::sec:    return state::cmd;
    case state::cmd:    return state::args;
    case state::args:   print_log("get_next_state: no matching edge\n"); exit(EXIT_FAILURE);
    default:            print_log("get_next_state: no matching edge\n"); exit(EXIT_FAILURE);
  }
}

void set_time_section(state cur_state, Job& job, int time) {
  switch (cur_state) {
    case state::hrs:
      job.hrs = time;
      break;
    case state::min:
      job.min = time;
      break;
    case state::sec:
      job.sec = time;
      break;
    default:
      break;
  }
}

bool IsDigit(char c) {
  return '0' <= c && c <= '9';
}

int CharToInt(char c) {
  return (c - '0');
}

void parse_command_line(const std::string& line, Job& job) {
  state current_state = state::hrs;

  bool started_reading_new_word = false;
  int arg_index = 0;

  int time_value = 0;
  std::string buffer;

  for (int i = 0; i < line.length(); ++i) {
    char current_symbol = line[i];

    if (current_state != state::cmd && current_state != state::args) {
      if (current_symbol == '*') {
        time_value = time_wildcard;
        continue;
      } else if (current_symbol == ':' || current_symbol == ' ') {
        set_time_section(current_state, job, time_value);
        if (current_state != state::sec || current_symbol == ' ') {
          current_state = get_next_state(current_state);
          time_value = 0;
        }
        continue;
      } else if (IsDigit(current_symbol)) {
        time_value = time_value * 10 + CharToInt(current_symbol);
      }
    } else {
      // current_state == state::cmd || state::args

      if (current_symbol == ' ') {
        if (started_reading_new_word) {
          if (current_state == state::cmd) {
            current_state = get_next_state(current_state);
            job.command = buffer;
            buffer = "";
          } else {
            arg_index++;
            job.args.push_back(buffer);
            buffer = "";
          }
          started_reading_new_word = false;
        } else {
          continue;
        }
      } else {
        started_reading_new_word = true;
        buffer += current_symbol;
      }
    }
  }

  if (started_reading_new_word) {
    if (current_state == state::cmd) {
      job.command = buffer;
    } else {
      arg_index++;
      job.args.push_back(buffer);
    }
  }


  std::string answer;
  if (job.hrs == time_wildcard) {
    answer += '*';
  } else {
    answer += std::to_string(job.hrs);
  }
  answer += ':';
  if (job.min == time_wildcard) {
    answer += '*';
  } else {
    answer += std::to_string(job.min);
  }
  answer += ':' + std::to_string(job.sec);

  answer += ' ';
  std::cout << answer;
  std::cout << job.command;
  for (int i = 0; i < job.args.size(); ++i) {
    std::cout << ' ' << job.args[i];
  }
  std::cout << std::endl;
}

void load_job_list_from_file(std::vector<Job>& job_list, const char* file_path) {
  // Flag of that we read
  std::ifstream file(file_path, std::ifstream::in);
  std::string buffer;

  job_list.clear();
  int i = 0;

  while(getline(file, buffer)) {
    job_list.emplace_back(Job());
    parse_command_line(buffer, job_list[i]);
    ++i;
  }

  file.close();
}

unsigned int get_file_modification_time(const char* file_path) {
  struct stat attrib;
  stat(file_path, &attrib);
  return attrib.st_ctime;
}

bool file_was_modified(const char* file_path, unsigned int last_recorded_time) {
  unsigned int time = get_file_modification_time(file_path);
  return last_recorded_time != time;
}

pid_t run_job(Job& job) {
  // Fork the current process and run the job in the child
  pid_t pid = fork();
  if (pid < 0) {
    print_log("[*] Error running job\n");
    exit(EXIT_FAILURE);
  }

  if (pid == 0) {
    // We're in the child process.
    // Now run the job

    // prepare correct argv[]
    int num_args = job.args.size(); // We store the command in argv[0]
    char** argv = new char*[num_args + 2];
    argv[0] = job.command.data();
    for (int i = 0; i < num_args; ++i) {
      argv[i + 1] = job.args[i].data();
    }
    argv[num_args + 1] = NULL;

    print_log("[*] Running the job...\n");
    int exec_err = execvp(argv[0], argv);

    if(exec_err)
    {
      print_log("[!]    Running the job failed!\n");
    }

    delete[] argv;

    exit(EXIT_SUCCESS);
  }

  return pid;
}

void kill_processes(const std::vector<pid_t>& processes) {
  for (int i = 0; i < processes.size(); ++i) {
    kill(processes[i], SIGKILL);
  }
}

bool time_matches(const Job& job, struct tm& lt) {
  bool hour_matches = (job.hrs == time_wildcard || job.hrs == lt.tm_hour);
  bool minute_matches = (job.min == time_wildcard || job.min == lt.tm_min);
  bool second_matches = (job.sec == lt.tm_sec);

  return hour_matches && minute_matches && second_matches;
}

int main(int argc, char** argv)
{
#ifndef DEBUG
  launch_daemon();
    set_signal_handler();
#endif // DEBUG

  print_log("[*] mycron daemon started\n");
  std::vector<Job> jobs;
  std::vector<pid_t> active_processes;

  unsigned int last_modification_time = get_file_modification_time(mycrontab_path);
  load_job_list_from_file(jobs, mycrontab_path);

  int i = 0;
  int N_sec = 100;
  while (i < N_sec) {
    // Wake up every second
    sleep(1);

    time_t mytime = time(NULL);
    struct tm localtime;

    // Get the local time written into 'localtime'
    localtime_r(&mytime, &localtime);

    // Print [ time ] to terminal if debug is set
#ifdef DEBUG
    char* buf = new char[32];

    strftime(buf, sizeof(buf), "%H:%M:%S", &localtime);
    printf("[ %s ]\n", buf);
    fflush(stdout);

    delete[] buf;
#endif // DEBUG

    // Run the jobs
    for (int i = 0; i < jobs.size(); ++i) {
      if (time_matches(jobs[i], localtime)) {
        pid_t pid = run_job(jobs[i]);

        // Save processes id to stop them later
        active_processes.push_back(pid);
      }
    }

    // Reload active job list if necessary
    unsigned int new_mod_time = get_file_modification_time(mycrontab_path);
    if (new_mod_time != last_modification_time) {
      print_log("[*] reloading jobs\n");

      load_job_list_from_file(jobs, mycrontab_path);

      // Kill active processes if they are still running
      kill_processes(active_processes);
      active_processes.clear();

      last_modification_time = new_mod_time;
    }
  }
  print_log("[*] mycron daemon terminated\n");

  exit(EXIT_SUCCESS);
}