/*
 *  Copyright 2016 Utkin Dmitry <loentar@gmail.com>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 *  This file is part of ngrest: http://github.com/loentar/ngrest
 */

#include <stdlib.h>
#include <string.h>
#include <memory.h>

#include "MemPool.h"

#define NGREST_MEMPOOL_CHUNK_RESERVE 64

namespace ngrest {

MemPool::MemPool(uint64_t chunkSize_):
    chunkSize(chunkSize_)
{
}

MemPool::~MemPool()
{
    free();
}

void MemPool::reset()
{
    if (chunks) {
        currChunk = chunks;
        for (int i = 0; i < chunksCount; ++i) {
            chunks[i].size = 0;
        }
        chunkIndex = 0;
    }
}

void MemPool::free()
{
    for (int i = 0; i < chunksCount; ++i)
        ::free(chunks[i].buffer);
    ::free(chunks);
    chunksCount = 0;
    chunksReserved = 0;
    chunks = nullptr;
    currChunk = nullptr;
}

bool MemPool::isClean() const
{
    return chunks == nullptr || (currChunk == chunks && currChunk->size == 0);
}

char* MemPool::putCString(const char* string, bool terminate)
{
    return putData(string, strlen(string) + (terminate ? 1 : 0));
}

char* MemPool::putCString(const char* string, uint64_t size, bool terminate)
{
    char* res = putData(string, size);
    if (terminate)
        putChar('\0');
    return res;
}

char* MemPool::putData(const char* data, uint64_t size)
{
    return reinterpret_cast<char*>(memcpy(grow(size), data, size));
}

void MemPool::trim()
{
    if (chunkIndex + 1 >= chunksCount)
        return;

    for (Chunk *curr = currChunk + 1; chunksCount > chunkIndex + 1; ++curr, --chunksCount) {
        ::free(curr->buffer);
        memset(curr, 0, sizeof(Chunk));
    }
}

MemPool::Chunk* MemPool::flatten(bool terminate)
{
    if (!chunksCount)
        return nullptr;

    uint64_t oldFirstChunkSize = chunks->size;
    uint64_t newSize = getSize();
    uint64_t newBufferSize = newSize + (terminate ? 1 : 0);  // +1 - string terminator
    if (newBufferSize > chunks->bufferSize) {
        char* newBuffer = reinterpret_cast<char*>(realloc(chunks->buffer, newBufferSize));
        if (newBuffer == nullptr)
            throw std::bad_alloc();
        chunks->buffer = newBuffer;
        chunks->bufferSize = newBufferSize;
    }

    char* pos = chunks->buffer + oldFirstChunkSize;

    for (Chunk* curr = (chunks + 1); curr != (currChunk + 1); pos += curr->size, ++curr) {
        memcpy(pos, curr->buffer, curr->size);
        ::free(curr->buffer);
        pos += curr->size;
        curr->buffer = nullptr;
        curr->size = 0;
        curr->bufferSize = 0;
    }
    chunksCount = 1;
    currChunk = chunks;
    chunks->size = newSize;
    chunkIndex = 0;

    if (terminate)
        chunks->buffer[newSize] = '\0'; // terminate with \0 for C strings

    return chunks;
}

void MemPool::reserve(uint64_t size)
{
    if (!chunksCount) {
        // allocate at least default chunk size to prevent future reallocations for small data
        newChunk(size > chunkSize ? size : chunkSize);
        return;
    }

    if (size < currChunk->bufferSize)
        return;

    char* newBuffer = reinterpret_cast<char*>(realloc(currChunk->buffer, size));
    if (newBuffer == nullptr)
        throw std::bad_alloc();
    currChunk->buffer = newBuffer;
    currChunk->bufferSize = size;
}


void MemPool::newChunk(uint64_t size)
{
    if ((chunksCount + 1) > chunksReserved) { // we have ran out of chunks
        const int newChunksReserved = chunksCount + NGREST_MEMPOOL_CHUNK_RESERVE;
        Chunk* newChunks = reinterpret_cast<Chunk*>(realloc(chunks, sizeof(Chunk) * static_cast<size_t>(newChunksReserved)));
        if (!newChunks)
            throw std::bad_alloc();

        memset(newChunks + chunksCount, 0, NGREST_MEMPOOL_CHUNK_RESERVE * sizeof(Chunk));

        chunks = newChunks;
        chunksReserved = newChunksReserved;
        currChunk = chunks + chunksCount;
    } else
        ++currChunk;

    ++chunksCount;
    ++chunkIndex;

    if (currChunk->bufferSize < size) {
        char* newBuffer = reinterpret_cast<char*>(realloc(currChunk->buffer, size));
        if (!newBuffer)
            throw std::bad_alloc();

        currChunk->buffer = newBuffer;
        currChunk->bufferSize = size;
    }

    currChunk->size = 0;
}

}
