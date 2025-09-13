#include <iostream>

#include<chrono>
#include"threadpool.h"
#include<thread>

using uLong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(int begin,int end)
		: begin_(begin)
		, end_(end)
	{}


	Any run() override
	{
		std::cout << "tid:" << std::this_thread::get_id() << "begin" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++)
			sum += i;

		std::cout << "tid:" << std::this_thread::get_id() << "end" << std::endl;

		return sum;
	}
private:
	int begin_;
	int end_;
};


int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(2);
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		uLong sum1 = res1.get().cast_<uLong>();
		std::cout << sum1 << std::endl;
	}
	std::cout << "main over!" << std::endl;
	getchar();

#if 0
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(4);

		//随着task执行完，task对象没了，依赖与task对象的res对象也没了
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		uLong sum1 = res1.get().cast_<uLong>();
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		std::cout << sum1 + sum2 + sum3 << std::endl;
	}

	/*pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());*/

	std::this_thread::sleep_for(std::chrono::seconds(5));

	getchar();
#endif

	return 0;
}