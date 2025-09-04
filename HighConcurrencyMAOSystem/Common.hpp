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

// 定义大块内存起始页的页号的类型(64位程序下 size_t 不够用了)
#ifdef _WIN64 // 64位下两个都有定义，32位下只有_WIN32被定义了，所以_WIN64放前面
typedef unsigned long long PAGE_ID; // 八字节
#elif _WIN32
typedef size_t PAGE_ID; // 四字节
#else
// linux下其它的宏
#endif

#ifdef _WIN32
#include <windows.h>
#else
// ...
#endif

// 能不用宏就不用宏
static const size_t MAX_BYTES = 256 * 1024;  // max_bytes 256kb
static const size_t NFREELIST = 208; // nfreelist 哈希桶总的桶数
static const size_t NPAGES = 129; // PageCache里的桶数(0号空出)
static const size_t PAGE_SHIFT = 13; // Page间的转换

inline static void* SystemAlloc(size_t kpage) // 去堆上按页申请空间
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux下brk mmap进程分配内存的系统调用等
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();
	return ptr;
}

inline static void SystemFree(void* ptr) // 直接将内存还给堆
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//linux下sbrk unmmap等
#endif
}


static void*& NextObj(void* obj) // 取头上4/8字节
{
	return *(void**)obj;
}
class FreeList // 管理切分好的小对象的自由链表
{
public:
	void Push(void* obj) // 头插
	{
		assert(obj);
		//*(void**)obj = _freeList; // 赋值给头4/8字节
		NextObj(obj) = _freeList; // 即上一行的注释
		_freeList = obj;

		++_size;
	}

	void PushRange(void* start, void* end, size_t n) // 头插一段范围的内存给_freeList
	{
		NextObj(end) = _freeList; // 赋值给end的头4/8字节
		_freeList = start;

		//// 测试验证+条件断点
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

		_size += n; // 这里在参数增加一个个数n
	}

	void PopRange(void*& start, void*& end, size_t n) // 头删
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

	void* Pop() // 头删
	{
		assert(_freeList);
		void* obj = _freeList;
		_freeList = NextObj(obj); // _freeList指向下一个

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

class SizeClass // 计算对象大小的对齐映射规则的类
{
public:
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128]					8byte对齐	     freelist[0,16)
	// [128+1,1024]				16byte对齐	     freelist[16,72)
	// [1024+1,8*1024]			128byte对齐	     freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐     freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte对齐   freelist[184,208)
	static inline size_t _RoundUp(size_t bytes, size_t alignNum) // alignNum对齐数
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
		
		return ((bytes + alignNum - 1) & ~(alignNum - 1)); // 即上面注释的代码
		// &上(alignNum-1)的取反,二进制后面的几个数都&成0了
		// 带入1-128,要与&的就是7的取反,就是后3位都成0,对齐到8的倍数了 -> 26 -> 26+8-1=33 -> 33 &(~7) = 32
		// 带入129-1024,要&的就是15的取反,就是后4位都成0,对齐到16的倍数了...
	}

	static inline size_t RoundUp(size_t size) // 返回size对齐以后的值
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
		else{ //大于256KB的按页对齐
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}

	//static inline size_t _Index(size_t bytes, size_t alignNum) // 看自己是哪个桶->一般写法
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
	static inline size_t _Index(size_t bytes, size_t align_shift) // 看自己是哪个桶->位运算写法
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
		// 1 + 7  8          -> 1左移3位是8, 8-1是7, 1到8 加7 = 8到15
		// 2      9
		// ...               -> 8到15 -> 右移三位相当于除8 -> 变为1 -> 再减1,即0号桶
		// 8      15

		// 9 + 7 16
		// 10
		// ...
		// 16    23
	}

	static inline size_t Index(size_t bytes) // 计算映射的哪一个自由链表桶
	{
		assert(bytes <= MAX_BYTES);
		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0]; // 加上0号桶里的数量
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

	static size_t NumMoveSize(size_t size) // 一次thread cache从中心缓存获取多少个
	{
		assert(size > 0);
		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
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

	static size_t NumMovePage(size_t size) // 计算一次向系统获取几个页
	{
		size_t num = NumMoveSize(size); // thread cache一次向central cache申请对象的个数上限
		size_t npage = num * size; // num个size大小的对象所需的字节数

		npage >>= PAGE_SHIFT; // 将字节数转换为页数，npage 除8再除1024
		if (npage == 0)
		{
			npage = 1;
		}
		return npage;
	}
};

struct Span // 管理多个连续页大块内存跨度的结构
{
	PAGE_ID _pageId = 0; // 大块内存起始页的页号
	size_t  _n = 0;      // 页的数量

	Span* _next = nullptr;	// 双向链表的结构
	Span* _prev = nullptr;

	size_t _useCount = 0; // 切好小块内存，被分配给thread cache的计数
	void* _freeList = nullptr;  // 切好的小块内存的自由链表

	bool _isUse = false; // 是否在被使用

	size_t _objSize = 0; //切好的小对象的大小
};

class SpanList // 双向带头循环链表 
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
		//if (pos == _head) // 条件断点+调用堆栈帮助调试
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
	std::mutex _mtx; // 桶锁
};