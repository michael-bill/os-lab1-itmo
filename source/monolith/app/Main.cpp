#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include <linux/sched.h>
#include <sys/syscall.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <sstream>

using namespace std;

namespace monolith::app {

vector<string> tokenizeInput(const string& input) {
    vector<string> tokens;
    string token;
    istringstream stream(input);
    while (stream >> token) {
      tokens.push_back(token);
    }
    return tokens;
}

void executeCommand(vector<char*>& args) {
    execvp(args[0], args.data());
}

void Shell() {
    while (true) {
        string userCmd;
        vector<char*> cmdArgs;
        cout << "> ";
        getline(cin, userCmd);
        if (userCmd == "exit" || userCmd == "q") {
            break;
        }
        vector<string> cmdTokens = tokenizeInput(userCmd);
        for (const auto& token : cmdTokens) {
            cmdArgs.push_back(strdup(token.c_str()));
        }
        cmdArgs.push_back(nullptr);

        auto startTime = std::chrono::high_resolution_clock::now();

        // Структура для передачи параметров в clone3
        struct clone_args cl_args = {
            .flags = 0,
            .pidfd = 0,
            .child_tid = 0,
            .parent_tid = 0,
            .exit_signal = SIGCHLD,
            .stack = 0,
            .stack_size = 0,
            .tls = 0,
            .set_tid = 0,
            .set_tid_size = 0,
            .cgroup = 0
        };

        pid_t processId = syscall(SYS_clone3, &cl_args, sizeof(cl_args));
        if (processId < 0) {
            cerr << "Forking process failed." << endl;
            exit(1);
        } else if (processId == 0) {
            executeCommand(cmdArgs);
            cerr << "Command execution failed: " << strerror(errno) << endl;
            exit(1);
        } else {
            int status;
            waitpid(processId, &status, 0);
            auto endTime = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsedTime = endTime - startTime;
            cout << "Elapsed time: " << elapsedTime.count() << " milliseconds." << endl;
        }
        for (char* arg : cmdArgs) {
            free(arg);
        }
    }
}

}  // namespace monolith::app

int main() {
    monolith::app::Shell();
    return 0;
}
