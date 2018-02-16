#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

using namespace std;

std::atomic<int> numberNames;
std::string lastName;
std::mutex lastNameMutex;

void printNumberPeopleMet() {
  while(1) {
    {
      std::lock_guard<std::mutex> guard(lastNameMutex);
      cout << "I've met " << numberNames << " people" << endl;
      cout << "Last person's name was " << lastName << endl;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

void readOneLine() {
  while(1) {
    string name;
    getline(std::cin, name);

    {
      std::lock_guard<std::mutex> guard(lastNameMutex);
      lastName = name;
    }

    cout << "Hello " << name << "!";
    numberNames++;
  }
}

int main(int argc, char* argv[]) {
  std::thread readLineThread(readOneLine);
  std::thread printNumThread(printNumberPeopleMet);
  cout << "after the thread" << endl;
  readLineThread.join();
  cout << "after the join" << endl;
}
