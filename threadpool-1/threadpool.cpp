#include"threadpool.h"

#include<thread>
#include<iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;

ThreadPool::ThreadPool()
	: initThreadSizes_(4)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, mode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}


ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	//�ȴ��̳߳����������̷߳��� ����״̬������ & ����ִ������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}


//�����̳߳�ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	mode_ = mode;
}


//����������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshHold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshHold;
}

//�����̳߳�cachedģʽ���߳�����������ֵ
void ThreadPool::setThreadSizeThreshHold(int threshHold)
{
	if (checkRunningState())
		return;
	if( mode_ == PoolMode::MODE_CACHED)
		threadSizeThreshHold_ = threshHold;
	
}

//��������̳߳�
Result ThreadPool::submitTask(std::shared_ptr<Task> task)
{
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�߳�ͨ�ţ��ȴ���������п���
	if (!notEmpty_.wait_for(lock, std::chrono::seconds(1),[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
	{
		std::cerr << "task queue is full" << std::endl;

		//return task_->getResult();  �߳�ִ����task��task����ͱ���������
		return Result(task,false);
	}

	//��������ӵ��������
	taskQue_.emplace(task);
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
		threads_.emplace(id,std::move(ptr));
		//�����߳�
		threads_[id]->start();
		//�����߳�����
        curThreadSize_++;
		idleThreadSize_++;
	}

	return Result(task);
}

//�����̳߳�
void ThreadPool::start(int initThreadSizes)
{
	isPoolRunning_ = true;

	initThreadSizes_ = initThreadSizes;

	curThreadSize_ = initThreadSizes;

	//�����̶߳����ʱ�򣬰��̺߳��������̶߳���
	for (int i = 0; i < initThreadSizes_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
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

//�����̺߳���
void ThreadPool::threadFunc(int threadid)
{
	/*std::cout << "begin thread tid:" << std::this_thread::get_id() << std::endl;

	std::cout << "end thread tid:" << std::this_thread::get_id() << std::endl;*/

	auto lastTime = std::chrono::high_resolution_clock().now();

	for(;;)
	{
		std::shared_ptr<Task> task;
		//���߳�ȡ�������Ӧ���ͷ������Ӹ��ֲ�������,���������߳��޷��������
		{
			//��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id() <<"���Ի�ȡ����"<< std::endl;

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
		if(task!=nullptr)
			task->exec();
        idleThreadSize_++;

		lastTime = std::chrono::high_resolution_clock().now();
	}
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

/////////////////////////////////////////////////
//�߳���
int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}


Thread::~Thread()
{

}

//�����߳�
void Thread::start()
{
	//����һ���߳���ִ���̺߳���
	std::thread t(func_,threadId_);
	t.detach(); //�����߳�
}

int Thread::getId() const
{
	return threadId_;
}

////////////////////////////////////////////////
//Task��

Task::Task()
	: result_(nullptr)
{}

void Task::exec()
{
	if(result_!=nullptr)
		result_->setVal(run());
}

void Task::setResult(Result* res)
{
	result_ = res;
}



//////////////////////////////////////////////
//Result��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: task_(task)
	, isValid_(isValid)
{
	task_->setResult(this);
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); //task������û��ִ���꣬����������û��߳�
	return std::move(any_);
}

void Result::setVal(Any any)
{
	this->any_ = std::move(any);
    sem_.post(); //����ִ�����ˣ�֪ͨ�û��߳�
}
