#pragma once

#include "Common.hpp"
#include "PageCache.hpp"

class CentralCache // 单例的饿汉模式
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}
	Span* GetOneSpan(SpanList& list, size_t byte_size); // 从page cache获取一个非空的span

	// 从中心缓存获batchNum个size大小的对象给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size); 

	// 将一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t byte_size);

private:
	SpanList _spanLists[NFREELIST];

private:
	CentralCache()
	{}
	CentralCache(const CentralCache&) = delete;
	static CentralCache _sInst;
};

/////////////////////////////////////////////下面是函数实现
CentralCache CentralCache::_sInst;

Span* CentralCache::GetOneSpan(SpanList& list, size_t size) // 获取一个非空的span
{
	// 查看当前的spanlist中是否有还有未分配对象的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else // 没有未分配的对象就往后走
		{
			it = it->_next;
		}
	}
	// 找后面要空间时先把central cache的锁解掉，别的线程释放内存就能访问这个桶
	list._mtx.unlock();

	// 走到这里说没有空闲span了，只能找page cache要
	PageCache::GetInstance()->_pageMtx.lock(); // 对page cache一整个加锁
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();

	// 对获取span进行切分，不需要加锁，因为此时别的线程访问不到这个span

	// 计算span的大块内存的起始地址 和大块内存的大小(字节数)
	char* start = (char*)(span->_pageId << PAGE_SHIFT); // 起始地址，如_pageId是100->100 * 8*1024
	size_t bytes = span->_n << PAGE_SHIFT; // 页数乘8K，和上一行一样
	char* end = start + bytes;

	// 把大块内存切成自由链表 链接起来
	span->_freeList = start; // 先切一块下来去做头，方便尾插
	start += size; // 加等到下一块
	void* tail = span->_freeList;
	int i = 1; // 调试第一次看是不是1024
	while (start < end)
	{
		++i;
		NextObj(tail) = start; // tail指向start
		tail = start; // tail = NextObj(tail; // 更新tail
		start += size; // start往后走
	}

	NextObj(tail) = nullptr;

	//// 测试验证+条件断点
	//int j = 0;
	//void* cur = (char*)(span->_pageId << PAGE_SHIFT);
	//while (cur)
	//{
	//	cur = NextObj(cur);
	//	++j;
	//}

	//if ((bytes / size) != j)
	//{
	//	int test = 0;
	//}

	// 切好span以后，需要把span挂到桶里面去的时候，再加锁
	list._mtx.lock();
	list.PushFront(span); // span头插到list
	return span;
}

// 从中心缓存获batchNum个size大小的对象给thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock(); // 加桶锁

	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	// 从span中获取batchNum个对象，如果不够batchNum个，有多少拿多少
	start = span->_freeList;
	end = start;
	size_t i = 0; // 在调试里测试切的块数
	size_t actualNum = 1; // 要返回的实际拿到的数量
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end); // 往后走，有多少拿多少
		++i;
		++actualNum;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;

	//// 测试验证+条件断点
	//int j = 0;
	//void* cur = start;
	//while (cur)
	//{
	//	cur = NextObj(cur);
	//	++j;
	//}

	//if (actualNum != j)
	//{
	//	int test = 0;
	//}

	_spanLists[index]._mtx.unlock();
	return actualNum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size); // 先算出是哪个桶
	_spanLists[index]._mtx.lock();
	while (start)
	{
		void* next = NextObj(start);

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;

		// 说明span的切分出去的所有小块内存都回来了
		// 这个span就可以再回收给page cache，pagecache可以再尝试去做前后页的合并
		if (span->_useCount == 0)
		{
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// 释放span给page cache时，使用page cache的锁就可以了
			_spanLists[index]._mtx.unlock(); // 这时把桶锁解掉

			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}

		start = next;
	}
	_spanLists[index]._mtx.unlock();
}