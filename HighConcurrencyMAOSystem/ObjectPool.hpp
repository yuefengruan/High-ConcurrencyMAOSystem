#pragma once

//#include <iostream>
//#include <vector>
#include "Common.hpp"

//using std::cout;
//using std::endl;

//#ifdef _WIN32
//#include <windows.h> // ֱ���Ҷ�����ռ�Ҫ����ͷ�ļ�
//#else
//// linux�µ�ͷ�ļ�...
//#endif
//
//inline static void* SystemAlloc(size_t kpage) // ȥ���ϰ�ҳ����ռ�(Ҳ������malloc)
//{
//#ifdef _WIN32
//	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); // ����13 ������8K
//#else
//	// linux��brk mmap���̷����ڴ��ϵͳ���õ�
//#endif
//
//	if (ptr == nullptr)
//		throw std::bad_alloc();
//
//	return ptr;
//}

template<class T>
class ObjectPool // �����ڴ��
{
public:
	T* New()
	{
		T* obj = nullptr;

		if (_freeList) // ���ȿ��������ڴ������ٴ��ظ����� -> ͷɾ
		{
			 // ȡͷ4/8�ĵ�ַ(_freeList����һ��)��next��Ȼ�����һ���ڴ��obj��Ȼ��������������next
			void* next = *((void**)_freeList); // _��int*�����64λ��������->��˳��һ������ָ��
			obj = (T*)_freeList;
			_freeList = next;
		}
		else // Ȼ��_memory��û�пռ�
		{
			if (_remainBytes < sizeof(T)) // ʣ���ڴ治��һ�������Сʱ�������¿����ռ�
			{
				_remainBytes = 128 * 1024;
				// _memory = (char*)malloc(_remainBytes);
				// ȥ���ϰ�ҳ����ռ�(Ҳ�����������malloc��Ч�ʲ�ֻ࣬��������malloc)
				_memory = (char*)SystemAlloc(_remainBytes >> 13); // ����13 ������8K
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize; // ���ٸ�һ��ָ��Ĵ�С
			_remainBytes -= objSize;
		}

		new(obj)T; // ��λnew -> ��ʾ����T�Ĺ��캯����ʼ��
		return obj;
	}

	void Delete(T* obj)
	{
		obj->~T(); // ��ʾ�������������������
		// ȡͷ4/8�ĵ�ַͷ�嵽��������������
		// *(int*)obj = _freeList; // ��int*�����64λ��������->��˳��һ������ָ��
		*(void**)obj = _freeList; // ͷ4/8�ֽ�ָ��_freeList
		_freeList = obj; // ����_freeList
	}

private:
	char* _memory = nullptr; // ָ�����ڴ��ָ��
	size_t _remainBytes = 0; // ����ڴ����зֹ�����ʣ���ֽ���
	void* _freeList = nullptr; // ���������������ӵ����������ͷָ��
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
//	const size_t Rounds = 7; // �����ͷŵ��ִ�
//	const size_t N = 100000; // ÿ�������ͷŶ��ٴ�
//	std::vector<TreeNode*> v1;
//	v1.reserve(N);
//	size_t begin1 = clock();
//	for (size_t j = 0; j < Rounds; ++j) // ��汾�������ͷ�Rounds�� N��
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
//	for (size_t j = 0; j < Rounds; ++j) // �Լ�ʵ�ֵ������ͷ�Rounds�� N��
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