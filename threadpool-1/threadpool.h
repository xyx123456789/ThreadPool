#pragma once

#include<vector>
#include<queue>
#include<atomic>
#include<memory>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>

//Any�࣬���ڴ洢�������͵�����
class Any {
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//������캯��������Any��洢�������͵�����
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)){}

    //�������������Any��洢�����ݱ�ȡ��
    template<typename T>
    T cast_() 
	{
		//��ô��base�ҵ�����ָ���Derive���󣬴�������ȡ��data_
		//����ָ�� -> ������ָ��
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
    }


private:

	//��������
	class Base {
	public:
		virtual ~Base() = default;
	private:
	};

	//����������
	template<typename T>
	class Derive :public Base {
	public:
		Derive(T data):data_(data){}

		T data_;
	};


private:
	std::unique_ptr<Base> base_;
};

//ʵ��һ���ź�����
class Semaphore {
public:
	Semaphore(int limit = 0):resLimit_(limit){}
	~Semaphore() = default;

	//��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ��������ǰ�߳�
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
        resLimit_--;
	}

	//����һ���ź�����Դ
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

//ʵ�ֽ����ύ���̳߳ص�task����ִ�����ķ���ֵ����
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//��ȡ����ִ����ķ���ֵ
	void setVal(Any any);

	//��ȡtask�ķ���ֵ
    Any get();

private:
	Any any_; //�洢����ķ���ֵ
	Semaphore sem_; //�߳�ͨ���ź���
	std::shared_ptr<Task> task_; //ָ���Ӧ��ȡ����ֵ���������
	bool isValid_; //�жϷ���ֵ�Ƿ���Ч
};


//����������
class Task {

public:
	Task();
	~Task() = default;

	void exec();
	void setResult(Result* res);

	//�û������Զ����������ͣ�ֻ��Ҫ�̳�Task�ಢʵ��run��������
	virtual Any run() = 0;
private:
	Result* result_;
};


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

	Thread(ThreadFunc func);
	~Thread();

	void start();

	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //�߳�id
};

//�̳߳�����
class ThreadPool {
public:
	ThreadPool();
	~ThreadPool();

	//��ϣ�����̳߳ؽ��п������߸�ֵ�������̳߳��漰�Ķ���̫��
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool operator=(const ThreadPool&) = delete;

	//�����̳߳�ģʽ
	void setMode(PoolMode mode);

	//����������������ֵ
    void setTaskQueMaxThreshHold(int threshHold);

	//�����̳߳�cachedģʽ���߳�����������ֵ
	void setThreadSizeThreshHold(int threshHold);

	//��������̳߳�
	Result submitTask(std::shared_ptr<Task> task);

	//�����̳߳�
	void start(int initThreadSizes = std::thread::hardware_concurrency());

private:
	//�����̺߳���
	void threadFunc(int threadid);

	//����̳߳��Ƿ���������
	bool checkRunningState() const ;

private:
	//std::vector<std::unique_ptr<Thread>> threads_; //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSizes_; //��ʼ�߳�����
	std::atomic_int curThreadSize_; //��¼��ǰ�̳߳������߳�����
	int threadSizeThreshHold_; //�߳�����������ֵ
	std::atomic_int idleThreadSize_; //��¼�����̵߳�����
	
	std::queue<std::shared_ptr<Task>> taskQue_; //�������
	std::atomic_int taskSize_; //��������
	int taskQueMaxThreshHold_; //������������ֵ

	std::mutex taskQueMtx_; //������л�����
	std::condition_variable notFull_; //������з�����������
    std::condition_variable notEmpty_; //������зǿ���������
	std::condition_variable exitCond_; //�̳߳��˳���������

    PoolMode mode_; //�̳߳�ģʽ
	std::atomic_bool isPoolRunning_; //��ʾ�̳߳ص�����״̬
};
