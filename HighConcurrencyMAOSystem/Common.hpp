#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <ctime>
#include <cassert>

#include <thread>
#include <mutex>
#include <atomic>

using std::cout;
using std::endl;

// �������ڴ���ʼҳ��ҳ�ŵ�����(64λ������ size_t ��������)
#ifdef _WIN64 // 64λ���������ж��壬32λ��ֻ��_WIN32�������ˣ�����_WIN64��ǰ��
typedef unsigned long long PAGE_ID; // ���ֽ�
#elif _WIN32
typedef size_t PAGE_ID; // ���ֽ�
#else
// linux�������ĺ�
#endif

#ifdef _WIN32
#include <windows.h>
#else
// ...
#endif

// �ܲ��ú�Ͳ��ú�
static const size_t MAX_BYTES = 256 * 1024;  // max_bytes 256kb
static const size_t NFREELIST = 208; // nfreelist ��ϣͰ�ܵ�Ͱ��
static const size_t NPAGES = 129; // PageCache���Ͱ��(0�ſճ�)
static const size_t PAGE_SHIFT = 13; // Page���ת��

inline static void* SystemAlloc(size_t kpage) // ȥ���ϰ�ҳ����ռ�
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux��brk mmap���̷����ڴ��ϵͳ���õ�
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();
	return ptr;
}

inline static void SystemFree(void* ptr) // ֱ�ӽ��ڴ滹����
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//linux��sbrk unmmap��
#endif
}


static void*& NextObj(void* obj) // ȡͷ��4/8�ֽ�
{
	return *(void**)obj;
}
class FreeList // �����зֺõ�С�������������
{
public:
	void Push(void* obj) // ͷ��
	{
		assert(obj);
		//*(void**)obj = _freeList; // ��ֵ��ͷ4/8�ֽ�
		NextObj(obj) = _freeList; // ����һ�е�ע��
		_freeList = obj;

		++_size;
	}

	void PushRange(void* start, void* end, size_t n) // ͷ��һ�η�Χ���ڴ��_freeList
	{
		NextObj(end) = _freeList; // ��ֵ��end��ͷ4/8�ֽ�
		_freeList = start;

		//// ������֤+�����ϵ�
		//int i = 0;
		//void* cur = start;
		//while (cur)
		//{
		//	cur = NextObj(cur);
		//	++i;
		//}

		//if (n != i)
		//{
		//	int test = 0;
		//}

		_size += n; // �����ڲ�������һ������n
	}

	void PopRange(void*& start, void*& end, size_t n) // ͷɾ
	{
		assert(n <= _size);
		start = _freeList;
		end = start;

		for (size_t i = 0; i < n - 1; ++i)
		{
			end = NextObj(end);
		}

		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	void* Pop() // ͷɾ
	{
		assert(_freeList);
		void* obj = _freeList;
		_freeList = NextObj(obj); // _freeListָ����һ��

		--_size;

		return obj;
	}

	bool Empty()
	{
		return _freeList == nullptr;
	}

	size_t& MaxSize()
	{
		return _maxSize;
	}

	size_t Size()
	{
		return _size;
	}

private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0;
};

class SizeClass // ��������С�Ķ���ӳ��������
{
public:
	// ������������10%���ҵ�����Ƭ�˷�
	// [1,128]					8byte����	     freelist[0,16)
	// [128+1,1024]				16byte����	     freelist[16,72)
	// [1024+1,8*1024]			128byte����	     freelist[72,128)
	// [8*1024+1,64*1024]		1024byte����     freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte����   freelist[184,208)
	static inline size_t _RoundUp(size_t bytes, size_t alignNum) // alignNum������
	{
		
		//size_t alignSize;
		//if (bytes % alignNum != 0)
		//{
		//	alignSize = (bytes / alignNum + 1) * alignNum;
		//}
		//else
		//{
		//	alignSize = bytes;
		//}
		//return alignSize;
		
		return ((bytes + alignNum - 1) & ~(alignNum - 1)); // ������ע�͵Ĵ���
		// &��(alignNum-1)��ȡ��,�����ƺ���ļ�������&��0��
		// ����1-128,Ҫ��&�ľ���7��ȡ��,���Ǻ�3λ����0,���뵽8�ı����� -> 26 -> 26+8-1=33 -> 33 &(~7) = 32
		// ����129-1024,Ҫ&�ľ���15��ȡ��,���Ǻ�4λ����0,���뵽16�ı�����...
	}

	static inline size_t RoundUp(size_t size) // ����size�����Ժ��ֵ
	{
		if (size <= 128) {
			return _RoundUp(size, 8);
		}
		else if (size <= 1024) {
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024) {
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024) {
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024) {
			return _RoundUp(size, 8 * 1024);
		}
		else{ //����256KB�İ�ҳ����
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}

	//static inline size_t _Index(size_t bytes, size_t alignNum) // ���Լ����ĸ�Ͱ->һ��д��
	//{
	//	if (bytes % alignNum == 0)
	//	{
	//		return bytes / alignNum - 1;
	//	}
	//	else
	//	{
	//		return bytes / alignNum;
	//	}
	//}
	static inline size_t _Index(size_t bytes, size_t align_shift) // ���Լ����ĸ�Ͱ->λ����д��
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
		// 1 + 7  8          -> 1����3λ��8, 8-1��7, 1��8 ��7 = 8��15
		// 2      9
		// ...               -> 8��15 -> ������λ�൱�ڳ�8 -> ��Ϊ1 -> �ټ�1,��0��Ͱ
		// 8      15

		// 9 + 7 16
		// 10
		// ...
		// 16    23
	}

	static inline size_t Index(size_t bytes) // ����ӳ�����һ����������Ͱ
	{
		assert(bytes <= MAX_BYTES);
		// ÿ�������ж��ٸ���
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0]; // ����0��Ͱ�������
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
			return -1;
		}
	}

	static size_t NumMoveSize(size_t size) // һ��thread cache�����Ļ����ȡ���ٸ�
	{
		assert(size > 0);
		// [2, 512]��һ�������ƶ����ٸ������(������)����ֵ
		// С����һ���������޸�
		// С����һ���������޵�
		int num = MAX_BYTES / size;
		if (num < 2)
		{
			num = 2;
		}
		if (num > 512)
		{
			num = 512;
		}
		return num;
	}

	static size_t NumMovePage(size_t size) // ����һ����ϵͳ��ȡ����ҳ
	{
		size_t num = NumMoveSize(size); // thread cacheһ����central cache�������ĸ�������
		size_t npage = num * size; // num��size��С�Ķ���������ֽ���

		npage >>= PAGE_SHIFT; // ���ֽ���ת��Ϊҳ����npage ��8�ٳ�1024
		if (npage == 0)
		{
			npage = 1;
		}
		return npage;
	}
};

struct Span // ����������ҳ����ڴ��ȵĽṹ
{
	PAGE_ID _pageId = 0; // ����ڴ���ʼҳ��ҳ��
	size_t  _n = 0;      // ҳ������

	Span* _next = nullptr;	// ˫������Ľṹ
	Span* _prev = nullptr;

	size_t _useCount = 0; // �к�С���ڴ棬�������thread cache�ļ���
	void* _freeList = nullptr;  // �кõ�С���ڴ����������

	bool _isUse = false; // �Ƿ��ڱ�ʹ��

	size_t _objSize = 0; //�кõ�С����Ĵ�С
};

class SpanList // ˫���ͷѭ������ 
{
public:
	SpanList()
	{
		_head = new Span;
		//_head = _spanPool.New();
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		// prev newspan pos
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);
		//if (pos == _head) // �����ϵ�+���ö�ջ��������
		//{
		//	int test = 0;
		//}

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}

private:
	Span* _head;
	//static ObjectPool<Span> _spanPool;
public:
	std::mutex _mtx; // Ͱ��
};