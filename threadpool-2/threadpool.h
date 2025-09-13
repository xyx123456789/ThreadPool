#pragma once

#include<vector>
#include<queue>
#include<atomic>
#include<memory>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<future>
#include<thread>
#include<iostream>
#include<unordered_map>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;




//线程池支持的模式
enum class PoolMode {
	MODE_FIXED, //固定大小线程池
	MODE_CACHED, //线程数量可动态增长
};

//线程类型
class Thread {
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{}
	~Thread() = default;

	void start()
	{
		//创建一个线程来执行线程函数
		std::thread t(func_, threadId_);
		t.detach(); //分离线程
	}

	int getId() const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //线程id
};

int Thread::generateId_ = 0;

//线程池类型
class ThreadPool {
public:
	ThreadPool()
		: initThreadSizes_(4)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, mode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
	{}
	~ThreadPool()
	{
		isPoolRunning_ = false;

		//等待线程池里面所有线程返回 两种状态：阻塞 & 正在执行任务
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
	}

	//不希望对线程池进行拷贝或者赋值操作，线程池涉及的东西太多
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool operator=(const ThreadPool&) = delete;

	//设置线程池模式
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		mode_ = mode;
	}

	//设置任务队列最大阈值
	void setTaskQueMaxThreshHold(int threshHold)
	{
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshHold;
	}

	//设置线程池cached模式下线程数量上限阈值
	void setThreadSizeThreshHold(int threshHold)
	{
		if (checkRunningState())
			return;
		if (mode_ == PoolMode::MODE_CACHED)
			threadSizeThreshHold_ = threshHold;
	}

	//添加任务到线程池
	//Result submitTask(std::shared_ptr<Task> task);
	template<typename Func,typename...Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//打包任务，放入任务队列里面
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		//获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//线程通信，等待任务队列有空余
		if (!notEmpty_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			std::cerr << "task queue is full" << std::endl;

			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		//将任务添加到任务队列
		//taskQue_.emplace(task);
		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;

		//通知其他线程，任务队列有任务了
		notFull_.notify_all();

		//cached模式，任务处理比较紧急，小而快的任务 需要根据任务数量和空闲线程的数量，判断是否需要填加线程
		if (mode_ == PoolMode::MODE_CACHED and
			taskSize_ > idleThreadSize_ and
			curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << "create new thread" << std::endl;

			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int id = ptr->getId();
			threads_.emplace(id, std::move(ptr));
			//启动线程
			threads_[id]->start();
			//更新线程数量
			curThreadSize_++;
			idleThreadSize_++;
		}

		return result;
	}

	//启动线程池
	void start(int initThreadSizes = std::thread::hardware_concurrency())
	{
		isPoolRunning_ = true;

		initThreadSizes_ = initThreadSizes;

		curThreadSize_ = initThreadSizes;

		//创建线程对象的时候，把线程函数给到线程对象
		for (int i = 0; i < initThreadSizes_; i++)
		{
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int id = ptr->getId();
			threads_.emplace(id, std::move(ptr));
		}

		//启动线程
		for (int i = 0; i < initThreadSizes_; i++)
		{
			threads_[i]->start();
			idleThreadSize_++; //刚启动，没提交任务，所以全是空闲线程
		}
	}

private:
	//定义线程函数
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		for (;;)
		{
			Task task;
			//当线程取出任务后应该释放锁，加个局部作用域,否则其他线程无法添加任务
			{
				//获取锁
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务" << std::endl;

				//空闲线程超过60s，进行回收
				while (taskQue_.size() == 0)
				{
					if (!isPoolRunning_)
					{
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_.notify_all();
						return;
					}

					if (mode_ == PoolMode::MODE_CACHED)
					{
						//条件变量超时返回
						if (std::cv_status::timeout ==
							notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME and curThreadSize_ > initThreadSizes_)
							{
								//线程对象从列表容器中删除
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;

								std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;

								return;
							}
						}
					}
					else
					{
						//等待notEmpty条件，_条件变量，等待任务队列有任务
						notEmpty_.wait(lock);
					}

					//线程池结束，回收线程资源
					/*if (!isPoolRunning_)
					{
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_.notify_all();
						return;
					}*/
				}

				idleThreadSize_--;

				std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功" << std::endl;

				//从任务队列中取一个任务
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//如果依然有剩余任务，通知其他线程执行任务
				if (taskQue_.size() > 0)
				{
					notFull_.notify_all();
				}


				//通知其他线程，任务队列有空余
				notFull_.notify_all();
			}

			//当前线程负责执行这个任务
			if (task != nullptr)
				task();
			idleThreadSize_++;

			lastTime = std::chrono::high_resolution_clock().now();
		}
	}

	//检查线程池是否正在运行
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}

private:
	//std::vector<std::unique_ptr<Thread>> threads_; //线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSizes_; //初始线程数量
	std::atomic_int curThreadSize_; //记录当前线程池里面线程数量
	int threadSizeThreshHold_; //线程数量上限阈值
	std::atomic_int idleThreadSize_; //记录空闲线程的数量

	using Task = std::function<void()>;
	std::queue<Task> taskQue_; //任务队列
	std::atomic_int taskSize_; //任务数量
	int taskQueMaxThreshHold_; //任务队列最大阈值

	std::mutex taskQueMtx_; //任务队列互斥锁
	std::condition_variable notFull_; //任务队列非满条件变量
	std::condition_variable notEmpty_; //任务队列非空条件变量
	std::condition_variable exitCond_; //线程池退出条件变量

	PoolMode mode_; //线程池模式
	std::atomic_bool isPoolRunning_; //表示线程池的启动状态
};
