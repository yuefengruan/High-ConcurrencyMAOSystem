#pragma once

//#include <iostream>
//#include <vector>
#include "Common.hpp"

//using std::cout;
//using std::endl;

//#ifdef _WIN32
//#include <windows.h> // 直接找堆申请空间要包的头文件
//#else
//// linux下的头文件...
//#endif
//
//inline static void* SystemAlloc(size_t kpage) // 去堆上按页申请空间(也可以用malloc)
//{
//#ifdef _WIN32
//	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // 左移13 即乘以8K
//#else
//	// linux下brk mmap进程分配内存的系统调用等
//#endif
//
//	if (ptr == nullptr)
//		throw std::bad_alloc();
//
//	return ptr;
//}

template<class T>
class ObjectPool // 定长内存池
{
public:
	T* New()
	{
		T* obj = nullptr;

		if (_freeList) // 优先看还回来内存块对象，再次重复利用 -> 头删
		{
			 // 取头4/8的地址(_freeList的下一个)给next，然后给第一块内存给obj，然后自由链表跳到next
			void* next = *((void**)_freeList); // _用int*如果是64位就有问题->用顺便一个二级指针
			obj = (T*)_freeList;
			_freeList = next;
		}
		else // 然后看_memory有没有空间
		{
			if (_remainBytes < sizeof(T)) // 剩余内存不够一个对象大小时，则重新开大块空间
			{
				_remainBytes = 128 * 1024;
				// _memory = (char*)malloc(_remainBytes);
				// 去堆上按页申请空间(也可以用上面的malloc，效率差不多，只是脱离了malloc)
				_memory = (char*)SystemAlloc(_remainBytes >> 13); // 右移13 即除以8K
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize; // 至少给一个指针的大小
			_remainBytes -= objSize;
		}

		new(obj)T; // 定位new -> 显示调用T的构造函数初始化
		return obj;
	}

	void Delete(T* obj)
	{
		obj->~T(); // 显示调用析构函数清理对象
		// 取头4/8的地址头插到自由链表保存起来
		// *(int*)obj = _freeList; // 用int*如果是64位就有问题->用顺便一个二级指针
		*(void**)obj = _freeList; // 头4/8字节指向_freeList
		_freeList = obj; // 更新_freeList
	}

private:
	char* _memory = nullptr; // 指向大块内存的指针
	size_t _remainBytes = 0; // 大块内存在切分过程中剩余字节数
	void* _freeList = nullptr; // 还回来过程中链接的自由链表的头指针
};


//struct TreeNode
//{
//	int _val;
//	TreeNode* _left;
//	TreeNode* _right;
//
//	TreeNode()
//		:_val(0)
//		, _left(nullptr)
//		, _right(nullptr)
//	{}
//};
//void TestObjectPool()
//{
//	const size_t Rounds = 7; // 申请释放的轮次
//	const size_t N = 100000; // 每轮申请释放多少次
//	std::vector<TreeNode*> v1;
//	v1.reserve(N);
//	size_t begin1 = clock();
//	for (size_t j = 0; j < Rounds; ++j) // 库版本的申请释放Rounds轮 N次
//	{
//		for (int i = 0; i < N; ++i)
//		{
//			v1.push_back(new TreeNode);
//		}
//		for (int i = 0; i < N; ++i)
//		{
//			delete v1[i];
//		}
//		v1.clear();
//	}
//	size_t end1 = clock();
//
//	std::vector<TreeNode*> v2;
//	v2.reserve(N);
//	ObjectPool<TreeNode> TNPool;
//	size_t begin2 = clock();
//	for (size_t j = 0; j < Rounds; ++j) // 自己实现的申请释放Rounds轮 N次
//	{
//		for (int i = 0; i < N; ++i)
//		{
//			v2.push_back(TNPool.New());
//		}
//		for (int i = 0; i < N; ++i)
//		{
//			TNPool.Delete(v2[i]);
//		}
//		v2.clear();
//	}
//	size_t end2 = clock();
//
//	cout << "new cost time:" << end1 - begin1 << endl;
//	cout << "my object pool cost time:" << end2 - begin2 << endl;
//}