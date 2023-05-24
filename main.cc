// #include "tlsf.h"
// #include "vcalloc/vcalloc.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <stdio.h>
#include <thread>

size_t static random_size() {
  static std::random_device dev;
  static std::mt19937 rng(dev());
  // static std::uniform_int_distribution<std::mt19937::result_type> dist(
  //     1024, 8 * 1024);
  static std::uniform_int_distribution<std::mt19937::result_type> dist(100,
                                                                       500);
  return dist(rng);
}

void test_new_delete() {
  for (int k = 0; k < 1000000; k++) {
    int *ptr = new int[random_size()];
    delete[] ptr;
  }
}

const int a = 1000;
const int b = 10;
int *ptr[a][b];

void test_write_read() {
  for (int i = 0; i < a; i++) {
    for (int j = 0; j < b; j++) {
      ptr[i][j] = new int;
      *ptr[i][j] = i * a + j;
    }
  }

  // check
  for (int i = 0; i < a; i++) {
    for (int j = 0; j < b; j++) {
      if (*ptr[i][j] != i * a + j) {
        std::cout << "error: " << i << " " << j << *ptr[i][j] << std::endl;
        return;
      }
    }
  }

  for (int i = 0; i < a; i++) {
    for (int j = 0; j < b; j++) {
      delete ptr[i][j];
    }
  }
}

void test_reuse() {
  int *a = new int;
  int *b = new int;
  int *c = new int[3];
  std::cout << a << std::endl;
  std::cout << b << std::endl;
  c[0] = 1;
  c[1] = 2;
  c[2] = 3;
  std::cout << c << std::endl;
  std::cout << &c[0] << " " << c[0] << std::endl;
  std::cout << &c[1] << " " << c[1] << std::endl;
  std::cout << &c[2] << " " << c[2] << std::endl;
  delete[] c;

  int *d = new int[3];
  d[0] = 4;
  d[1] = 5;
  d[2] = 6;
  std::cout << d << std::endl;
  std::cout << &d[0] << " " << d[0] << std::endl;
  std::cout << &d[1] << " " << d[1] << std::endl;
  std::cout << &d[2] << " " << d[2] << std::endl;
  delete[] d;
  delete a;
  delete b;
}

void test_multi_thread() {
  for (int i = 0; i < 10000; i++) {
    std::thread t1([] { test_reuse(); });
    t1.join();
  }
}

int main() {
  using std::chrono::duration;
  using std::chrono::duration_cast;
  using std::chrono::high_resolution_clock;
  using std::chrono::microseconds;
  using std::chrono::milliseconds;
  using std::chrono::nanoseconds;

  auto t1 = high_resolution_clock::now();
  test_new_delete();
  // test_write_read();
  // test_reuse();
  // test_multi_thread();
  auto t2 = high_resolution_clock::now();
  auto t = duration_cast<microseconds>(t2 - t1);
  std::cout << t.count() << "us\n";
}
