#pragma once

#include "Common.hpp"
#include "PageCache.hpp"

class CentralCache // �����Ķ���ģʽ
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}
	Span* GetOneSpan(SpanList& list, size_t byte_size); // ��page cache��ȡһ���ǿյ�span

	// �����Ļ����batchNum��size��С�Ķ����thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size); 

	// ��һ�������Ķ����ͷŵ�span���
	void ReleaseListToSpans(void* start, size_t byte_size);

private:
	SpanList _spanLists[NFREELIST];

private:
	CentralCache()
	{}
	CentralCache(const CentralCache&) = delete;
	static CentralCache _sInst;
};

/////////////////////////////////////////////�����Ǻ���ʵ��
CentralCache CentralCache::_sInst;

Span* CentralCache::GetOneSpan(SpanList& list, size_t size) // ��ȡһ���ǿյ�span
{
	// �鿴��ǰ��spanlist���Ƿ��л���δ��������span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else // û��δ����Ķ����������
		{
			it = it->_next;
		}
	}
	// �Һ���Ҫ�ռ�ʱ�Ȱ�central cache�������������߳��ͷ��ڴ���ܷ������Ͱ
	list._mtx.unlock();

	// �ߵ�����˵û�п���span�ˣ�ֻ����page cacheҪ
	PageCache::GetInstance()->_pageMtx.lock(); // ��page cacheһ��������
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();

	// �Ի�ȡspan�����з֣�����Ҫ��������Ϊ��ʱ����̷߳��ʲ������span

	// ����span�Ĵ���ڴ����ʼ��ַ �ʹ���ڴ�Ĵ�С(�ֽ���)
	char* start = (char*)(span->_pageId << PAGE_SHIFT); // ��ʼ��ַ����_pageId��100->100 * 8*1024
	size_t bytes = span->_n << PAGE_SHIFT; // ҳ����8K������һ��һ��
	char* end = start + bytes;

	// �Ѵ���ڴ��г��������� ��������
	span->_freeList = start; // ����һ������ȥ��ͷ������β��
	start += size; // �ӵȵ���һ��
	void* tail = span->_freeList;
	int i = 1; // ���Ե�һ�ο��ǲ���1024
	while (start < end)
	{
		++i;
		NextObj(tail) = start; // tailָ��start
		tail = start; // tail = NextObj(tail; // ����tail
		start += size; // start������
	}

	NextObj(tail) = nullptr;

	//// ������֤+�����ϵ�
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

	// �к�span�Ժ���Ҫ��span�ҵ�Ͱ����ȥ��ʱ���ټ���
	list._mtx.lock();
	list.PushFront(span); // spanͷ�嵽list
	return span;
}

// �����Ļ����batchNum��size��С�Ķ����thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock(); // ��Ͱ��

	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	// ��span�л�ȡbatchNum�������������batchNum�����ж����ö���
	start = span->_freeList;
	end = start;
	size_t i = 0; // �ڵ���������еĿ���
	size_t actualNum = 1; // Ҫ���ص�ʵ���õ�������
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end); // �����ߣ��ж����ö���
		++i;
		++actualNum;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;

	//// ������֤+�����ϵ�
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
	size_t index = SizeClass::Index(size); // ��������ĸ�Ͱ
	_spanLists[index]._mtx.lock();
	while (start)
	{
		void* next = NextObj(start);

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;

		// ˵��span���зֳ�ȥ������С���ڴ涼������
		// ���span�Ϳ����ٻ��ո�page cache��pagecache�����ٳ���ȥ��ǰ��ҳ�ĺϲ�
		if (span->_useCount == 0)
		{
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// �ͷ�span��page cacheʱ��ʹ��page cache�����Ϳ�����
			_spanLists[index]._mtx.unlock(); // ��ʱ��Ͱ�����

			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}

		start = next;
	}
	_spanLists[index]._mtx.unlock();
}