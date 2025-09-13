#pragma once

#include<vector>
#include<queue>
#include<atomic>
#include<memory>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>

//Any类，用于存储任意类型的数据
class Any {
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//这个构造函数可以让Any类存储任意类型的数据
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)){}

    //这个函数可以让Any类存储的数据被取出
    template<typename T>
    T cast_() 
	{
		//怎么从base找到它所指向的Derive对象，从它里面取出data_
		//基类指针 -> 派生类指针
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
    }


private:

	//基类类型
	class Base {
	public:
		virtual ~Base() = default;
	private:
	};

	//派生类类型
	template<typename T>
	class Derive :public Base {
	public:
		Derive(T data):data_(data){}

		T data_;
	};


private:
	std::unique_ptr<Base> base_;
};

//实现一个信号量类
class Semaphore {
public:
	Semaphore(int limit = 0):resLimit_(limit){}
	~Semaphore() = default;

	//获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//等待信号量有资源，没有资源则阻塞当前线程
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
        resLimit_--;
	}

	//增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;

//实现接受提交到线程池的task任务执行完后的返回值类型
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//获取任务执行完的返回值
	void setVal(Any any);

	//获取task的返回值
    Any get();

private:
	Any any_; //存储任务的返回值
	Semaphore sem_; //线程通信信号量
	std::shared_ptr<Task> task_; //指向对应获取返回值的任务对象
	bool isValid_; //判断返回值是否有效
};


//任务抽象基类
class Task {

public:
	Task();
	~Task() = default;

	void exec();
	void setResult(Result* res);

	//用户可以自定义任务类型，只需要继承Task类并实现run方法即可
	virtual Any run() = 0;
private:
	Result* result_;
};


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

	Thread(ThreadFunc func);
	~Thread();

	void start();

	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //线程id
};

//线程池类型
class ThreadPool {
public:
	ThreadPool();
	~ThreadPool();

	//不希望对线程池进行拷贝或者赋值操作，线程池涉及的东西太多
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool operator=(const ThreadPool&) = delete;

	//设置线程池模式
	void setMode(PoolMode mode);

	//设置任务队列最大阈值
    void setTaskQueMaxThreshHold(int threshHold);

	//设置线程池cached模式下线程数量上限阈值
	void setThreadSizeThreshHold(int threshHold);

	//添加任务到线程池
	Result submitTask(std::shared_ptr<Task> task);

	//启动线程池
	void start(int initThreadSizes = std::thread::hardware_concurrency());

private:
	//定义线程函数
	void threadFunc(int threadid);

	//检查线程池是否正在运行
	bool checkRunningState() const ;

private:
	//std::vector<std::unique_ptr<Thread>> threads_; //线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSizes_; //初始线程数量
	std::atomic_int curThreadSize_; //记录当前线程池里面线程数量
	int threadSizeThreshHold_; //线程数量上限阈值
	std::atomic_int idleThreadSize_; //记录空闲线程的数量
	
	std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
	std::atomic_int taskSize_; //任务数量
	int taskQueMaxThreshHold_; //任务队列最大阈值

	std::mutex taskQueMtx_; //任务队列互斥锁
	std::condition_variable notFull_; //任务队列非满条件变量
    std::condition_variable notEmpty_; //任务队列非空条件变量
	std::condition_variable exitCond_; //线程池退出条件变量

    PoolMode mode_; //线程池模式
	std::atomic_bool isPoolRunning_; //表示线程池的启动状态
};
