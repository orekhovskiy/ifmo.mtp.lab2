#include <pthread.h>
#include <cstdlib>
#include <vector>
#include <string>
#include <iostream>
#include <atomic>
#include <map>
#include <cassert>
#include <sstream>
#include <unistd.h>


struct consume_args_t
{
	pthread_mutex_t* mutex;
	bool is_debug;
	bool* exit;
	bool* updated;
	long long int* value;
	int* updated_id;
	int sleep;

	void set(
		pthread_mutex_t* mutex,
		bool is_debug,
		bool* exit,
		bool* updated,
		long long int* value,
		int* updated_id,
		int sleep)
	{
		this->mutex = mutex;
		this->is_debug = is_debug;
		this->exit = exit;
		this->updated = updated;
		this->value = value;
		this->updated_id = updated_id;
		this->sleep = sleep;
	}
};

struct produce_args_t
{
	pthread_mutex_t* mutex;
	bool* updated;
	bool* exit;
	long long int* value;
	int consume_threads_count;

	produce_args_t(
		pthread_mutex_t* mutex,
		bool* updated,
		bool* exit,
		long long* value,
		const int consume_threads_count)
		:
		mutex(mutex),
		updated(updated),
		exit(exit), value(value),
		consume_threads_count(consume_threads_count)
	{
	}
};

struct interrupt_args_t
{
	int consume_threads_count;
	int* updated_id;
	bool* exit;

	interrupt_args_t(
		bool* exit,
		int* updated_id,
		const int consume_threads_count)
		:
		consume_threads_count(consume_threads_count),
		updated_id(updated_id),
		exit(exit)
	{
	}
};


int run_treads(int consume_threads_count, int delay_time, bool is_debug);
int get_tid();
void* producer_routine(void* arg);
void* consumer_routine(void* arg);
void* consumer_interrupter_routine(void* arg);
int read_numbers(const std::string& s, std::vector<long long>& v);

int main(const int argc, char* argv[])
{
	int consume_threads_count_pos, delay_time_pos;
	auto is_debug = false;
	if (argc == 4 && std::string(argv[1]) == "-debug")
	{
		is_debug = true;
		consume_threads_count_pos = 2;
		delay_time_pos = 3;
	}
	else if (argc == 4 && std::string(argv[3]) == "-debug")
	{
		is_debug = true;
		consume_threads_count_pos = 1;
		delay_time_pos = 2;
	}
	else if (argc == 3)
	{
		consume_threads_count_pos = 1;
		delay_time_pos = 2;
	}
	else
	{
		return 1;
	}
	const auto consume_threads_count = strtol(argv[consume_threads_count_pos], nullptr, 10);
	const auto delay_time = strtol(argv[delay_time_pos], nullptr, 10);
	if (consume_threads_count < 1 || delay_time < 0)
	{
		return 1;
	}
	std::cout << run_treads(consume_threads_count, delay_time, is_debug) << std::endl;
}

int run_treads(const int consume_threads_count, int delay_time, const bool is_debug)
{
	pthread_mutex_t mutex;
	std::map<pthread_t, int> heap;
	auto updated = false, exit = false;
	long long int value = 0;
	auto updated_id = 0;
	std::vector<pthread_t> threads(consume_threads_count + 2);
	delay_time *= 1000;
	std::vector<consume_args_t> consume_arg(consume_threads_count);
	assert(pthread_mutex_init(&mutex, nullptr) == 0);
	for (auto i = 0; i < consume_threads_count; i++)
	{
		consume_arg[i].set(&mutex, is_debug, &exit, &updated, &value, &updated_id, delay_time);
		pthread_create(&threads[i + 2], nullptr, consumer_routine, static_cast<void*>(&consume_arg.at(i)));
	}

	produce_args_t produce_arg(&mutex, &updated, &exit, &value, consume_threads_count);
	pthread_create(&threads[0], nullptr, producer_routine, static_cast<void*>(&produce_arg));

	interrupt_args_t interrupt_arg(&exit, &updated_id, consume_threads_count);
	pthread_create(&threads[1], nullptr, consumer_interrupter_routine, static_cast<void*>(&interrupt_arg));

	auto result = 0, sum = 0;
	pthread_join(threads[0], nullptr);
	pthread_join(threads[1], nullptr);
	for (auto i = 0; i < consume_threads_count; i++)
	{
		pthread_join(threads[i + 2], reinterpret_cast<void**>(&result));
		sum += result;
	}

	return sum;
}

int get_tid()
{
	static std::atomic<int> count_threads;
	thread_local int id = count_threads++;
	return id;
}

void* producer_routine(void* arg)
{
	auto* produce_arg = static_cast<produce_args_t*>(arg);

	std::string str;
	std::vector<long long> numbers;
	std::getline(std::cin, str);
	const auto numbers_count = read_numbers(str, numbers);
	std::string line_part;

	for (auto pos = 0; pos < numbers_count; pos++)
	{
		while (*produce_arg->updated)
		{
			usleep(0);
		}
		*produce_arg->value = numbers[pos];
		*produce_arg->updated = true;
	}
	*produce_arg->exit = true;
	pthread_exit(nullptr);
	return nullptr;
}

void* consumer_routine(void* arg)
{
	auto* consume_arg = static_cast<consume_args_t*>(arg);
	long long int sum = 0;
	while (!(*consume_arg->exit) || *consume_arg->updated)
	{
		if (*consume_arg->updated)
		{
			pthread_mutex_lock(consume_arg->mutex);
			if (*consume_arg->updated)
			{
				sum += *consume_arg->value;
				*consume_arg->updated = false;
				pthread_mutex_unlock(consume_arg->mutex);
				if (consume_arg->is_debug)
				{
					std::cout << "(" + std::to_string(get_tid()) +  ", " + std::to_string(sum) + ")\n";
				}
				if (*consume_arg->updated_id != get_tid())
				{
					usleep(consume_arg->sleep == 0 ? 0 : rand() % consume_arg->sleep);
				}
				else
				{
					*consume_arg->updated_id = 0;
				}
			}
			else
			{
				pthread_mutex_unlock(consume_arg->mutex);
			}
		}
		usleep(100);
	}
	pthread_exit(reinterpret_cast<void*>(sum));
	return nullptr;
}

void* consumer_interrupter_routine(void* arg)
{
	auto* interrupt_arg = static_cast<interrupt_args_t*>(arg);
	while (!*interrupt_arg->exit)
	{
		if (*interrupt_arg->updated_id == 0)
		{
			*interrupt_arg->updated_id = rand() % interrupt_arg->consume_threads_count;
		}
		usleep(0);
	}
	pthread_exit(nullptr);
	return nullptr;
}

int read_numbers(const std::string& s, std::vector<long long>& v)
{
	std::istringstream is(s);
	int n;
	while (is >> n)
	{
		v.push_back(n);
	}
	return v.size();
}
