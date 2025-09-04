#pragma once

#include "Common.hpp"
#include "CentralCache.hpp"

class ThreadCache
{
public:
	void* FetchFromCentralCache(size_t index, size_t size); // �������ڴ��ȡ����

	void* Allocate(size_t size); // �����ڴ����
	void Deallocate(void* ptr, size_t size); // �ͷ��ڴ����

	void ListTooLong(FreeList& list, size_t size);
private:
	FreeList _freeLists[NFREELIST];
};

// TLS thread local storage �̱߳��ش洢
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;
// ����pTLSThreadCacheΪһ���̱߳��ش洢�ľ�̬����������ʼ��Ϊnullptr

/////////////////////////////////////////////�����Ǻ���ʵ��

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size) // �������ڴ��ȡ����
{
	// ����ʼ���������㷨
	// 1���ʼ����һ����central cacheһ������Ҫ̫�࣬��ΪҪ̫���˿����ò���
	// 2������㲻��Ҫ���size��С�ڴ�������ôbatchNum�ͻ᲻��������ֱ������
	// 3��sizeԽ��һ����central cacheҪ��batchNum��ԽС
	// 4��sizeԽС��һ����central cacheҪ��batchNum��Խ��
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));
	// windows.h����һ��min�ĺ꣬minǰ���ܼ�std::��
	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr; // ��CentralCache������ȡbatchNum��size��С�Ŀռ�
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0); // actualNum��ʵ�ʵõ��ģ���һ���õ�batchNum����

	if (actualNum == 1)
	{
		assert(start == end);
	}
	else // ����1������Ĺҵ���������
	{
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
	}
	return start;
}

void* ThreadCache::Allocate(size_t size) // �����ڴ����
{
	assert(size <= MAX_BYTES);
	size_t alignSize = SizeClass::RoundUp(size);// ���size�����Ժ��ֵ
	size_t index = SizeClass::Index(size); // ���ӳ�����һ����������Ͱ

	if (!_freeLists[index].Empty()) // ������������о�ֱ�ӷ��ز���ͷɾһ��
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alignSize); // ����һ���ȡ����
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size) // �ͷ��ڴ����
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	// �Ҷ�ӳ�����������Ͱ������������
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	// �������ȴ���һ������������ڴ�ʱ�Ϳ�ʼ��һ��list��central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize()) // Ҫ����Size�ӿ�
	{
		ListTooLong(_freeLists[index], size);
	}
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize()); // ȡ�������ڴ棬Ҫ����PopRange�ӿ�

	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}