#pragma once

#include "Common.hpp"
#include "CentralCache.hpp"

class ThreadCache
{
public:
	void* FetchFromCentralCache(size_t index, size_t size); // 从中心内存获取对象

	void* Allocate(size_t size); // 申请内存对象
	void Deallocate(void* ptr, size_t size); // 释放内存对象

	void ListTooLong(FreeList& list, size_t size);
private:
	FreeList _freeLists[NFREELIST];
};

// TLS thread local storage 线程本地存储
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;
// 声明pTLSThreadCache为一个线程本地存储的静态变量，并初始化为nullptr

/////////////////////////////////////////////下面是函数实现

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size) // 从中心内存获取对象
{
	// 慢开始反馈调节算法
	// 1、最开始不会一次向central cache一次批量要太多，因为要太多了可能用不完
	// 2、如果你不断要这个size大小内存需求，那么batchNum就会不断增长，直到上限
	// 3、size越大，一次向central cache要的batchNum就越小
	// 4、size越小，一次向central cache要的batchNum就越大
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));
	// windows.h里有一个min的宏，min前不能加std::了
	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr; // 找CentralCache批量获取batchNum个size大小的空间
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0); // actualNum是实际得到的（不一定得到batchNum个）

	if (actualNum == 1)
	{
		assert(start == end);
	}
	else // 大于1，其余的挂到自由链表
	{
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
	}
	return start;
}

void* ThreadCache::Allocate(size_t size) // 申请内存对象
{
	assert(size <= MAX_BYTES);
	size_t alignSize = SizeClass::RoundUp(size);// 算出size对齐以后的值
	size_t index = SizeClass::Index(size); // 算出映射的哪一个自由链表桶

	if (!_freeLists[index].Empty()) // 如果自由链表有就直接返回并且头删一个
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alignSize); // 从下一层获取对象
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size) // 释放内存对象
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	// 找对映射的自由链表桶，对象插入进入
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	// 当链表长度大于一次批量申请的内存时就开始还一段list给central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize()) // 要增加Size接口
	{
		ListTooLong(_freeLists[index], size);
	}
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize()); // 取批量的内存，要增加PopRange接口

	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}