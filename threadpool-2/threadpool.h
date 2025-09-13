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




//�̳߳�֧�ֵ�ģʽ
enum class PoolMode {
	MODE_FIXED, //�̶���С�̳߳�
	MODE_CACHED, //�߳������ɶ�̬����
};

//�߳�����
class Thread {
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{}
	~Thread() = default;

	void start()
	{
		//����һ���߳���ִ���̺߳���
		std::thread t(func_, threadId_);
		t.detach(); //�����߳�
	}

	int getId() const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //�߳�id
};

int Thread::generateId_ = 0;

//�̳߳�����
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

		//�ȴ��̳߳����������̷߳��� ����״̬������ & ����ִ������
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
	}

	//��ϣ�����̳߳ؽ��п������߸�ֵ�������̳߳��漰�Ķ���̫��
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool operator=(const ThreadPool&) = delete;

	//�����̳߳�ģʽ
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		mode_ = mode;
	}

	//����������������ֵ
	void setTaskQueMaxThreshHold(int threshHold)
	{
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshHold;
	}

	//�����̳߳�cachedģʽ���߳�����������ֵ
	void setThreadSizeThreshHold(int threshHold)
	{
		if (checkRunningState())
			return;
		if (mode_ == PoolMode::MODE_CACHED)
			threadSizeThreshHold_ = threshHold;
	}

	//��������̳߳�
	//Result submitTask(std::shared_ptr<Task> task);
	template<typename Func,typename...Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//������񣬷��������������
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		//��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//�߳�ͨ�ţ��ȴ���������п���
		if (!notEmpty_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			std::cerr << "task queue is full" << std::endl;

			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		//��������ӵ��������
		//taskQue_.emplace(task);
		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;

		//֪ͨ�����̣߳����������������
		notFull_.notify_all();

		//cachedģʽ��������ȽϽ�����С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ����߳�
		if (mode_ == PoolMode::MODE_CACHED and
			taskSize_ > idleThreadSize_ and
			curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << "create new thread" << std::endl;

			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int id = ptr->getId();
			threads_.emplace(id, std::move(ptr));
			//�����߳�
			threads_[id]->start();
			//�����߳�����
			curThreadSize_++;
			idleThreadSize_++;
		}

		return result;
	}

	//�����̳߳�
	void start(int initThreadSizes = std::thread::hardware_concurrency())
	{
		isPoolRunning_ = true;

		initThreadSizes_ = initThreadSizes;

		curThreadSize_ = initThreadSizes;

		//�����̶߳����ʱ�򣬰��̺߳��������̶߳���
		for (int i = 0; i < initThreadSizes_; i++)
		{
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int id = ptr->getId();
			threads_.emplace(id, std::move(ptr));
		}

		//�����߳�
		for (int i = 0; i < initThreadSizes_; i++)
		{
			threads_[i]->start();
			idleThreadSize_++; //��������û�ύ��������ȫ�ǿ����߳�
		}
	}

private:
	//�����̺߳���
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		for (;;)
		{
			Task task;
			//���߳�ȡ�������Ӧ���ͷ������Ӹ��ֲ�������,���������߳��޷��������
			{
				//��ȡ��
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����" << std::endl;

				//�����̳߳���60s�����л���
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
						//����������ʱ����
						if (std::cv_status::timeout ==
							notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME and curThreadSize_ > initThreadSizes_)
							{
								//�̶߳�����б�������ɾ��
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
						//�ȴ�notEmpty������_�����������ȴ��������������
						notEmpty_.wait(lock);
					}

					//�̳߳ؽ����������߳���Դ
					/*if (!isPoolRunning_)
					{
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_.notify_all();
						return;
					}*/
				}

				idleThreadSize_--;

				std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;

				//�����������ȡһ������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//�����Ȼ��ʣ������֪ͨ�����߳�ִ������
				if (taskQue_.size() > 0)
				{
					notFull_.notify_all();
				}


				//֪ͨ�����̣߳���������п���
				notFull_.notify_all();
			}

			//��ǰ�̸߳���ִ���������
			if (task != nullptr)
				task();
			idleThreadSize_++;

			lastTime = std::chrono::high_resolution_clock().now();
		}
	}

	//����̳߳��Ƿ���������
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}

private:
	//std::vector<std::unique_ptr<Thread>> threads_; //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSizes_; //��ʼ�߳�����
	std::atomic_int curThreadSize_; //��¼��ǰ�̳߳������߳�����
	int threadSizeThreshHold_; //�߳�����������ֵ
	std::atomic_int idleThreadSize_; //��¼�����̵߳�����

	using Task = std::function<void()>;
	std::queue<Task> taskQue_; //�������
	std::atomic_int taskSize_; //��������
	int taskQueMaxThreshHold_; //������������ֵ

	std::mutex taskQueMtx_; //������л�����
	std::condition_variable notFull_; //������з�����������
	std::condition_variable notEmpty_; //������зǿ���������
	std::condition_variable exitCond_; //�̳߳��˳���������

	PoolMode mode_; //�̳߳�ģʽ
	std::atomic_bool isPoolRunning_; //��ʾ�̳߳ص�����״̬
};
