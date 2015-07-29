/*******************************************************************************
 * tests/data/file_test.cpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chuck Norris can compile it.
 ******************************************************************************/

#include <c7a/common/string.hpp>
#include <c7a/data/block_queue.hpp>
#include <c7a/data/file.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace c7a;

TEST(File, PutSomeItemsGetItems) {

    // construct File with very small blocks for testing
    using File = data::FileBase<16>;
    File file;

    {
        File::Writer fw = file.GetWriter();
        fw.MarkItem();
        fw.Append("testtest");
        fw.MarkItem();
        fw.PutVarint(123456u);
        fw.MarkItem();
        fw.PutString("test1test2test3");
        fw.MarkItem();
        // long item spanning multiple blocks
        fw.PutString(std::string(64, '1'));
        fw.MarkItem();
        fw.Put<uint16_t>(42);
    }

    ASSERT_EQ(file.NumBlocks(), 6u);
    ASSERT_EQ(file.NumItems(), 5u);
    ASSERT_EQ(file.TotalBytes(), 6u * 16u);

    ASSERT_EQ(file.virtual_block(0).size(), 16u);
    ASSERT_EQ(file.virtual_block(1).size(), 16u);
    ASSERT_EQ(file.virtual_block(2).size(), 16u);
    ASSERT_EQ(file.virtual_block(3).size(), 16u);
    ASSERT_EQ(file.virtual_block(4).size(), 16u);
    ASSERT_EQ(file.virtual_block(5).size(), 14u);

    const unsigned char block_data_bytes[] = {
        // fw.Append("testtest");
        0x74, 0x65, 0x73, 0x74, 0x74, 0x65, 0x73, 0x74,
        // fw.PutVarint(123456u);
        0xC0, 0xC4, 0x07,
        // fw.PutString("test1test2test3");
        0x0F,
        0x74, 0x65, 0x73, 0x74, 0x31, 0x74, 0x65, 0x73,
        0x74, 0x32, 0x74, 0x65, 0x73, 0x74, 0x33,
        // fw.PutString(std::string(64, '1'));
        0x40,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
        // fw.Put<uint16_t>(42);
        0x2A, 0x00
    };

    if (0) {
        for (size_t i = 0; i != file.NumBlocks(); ++i) {
            std::cout << common::hexdump(file.virtual_block(i).ToString())
                      << std::endl;
        }
    }

    std::string block_data(reinterpret_cast<const char*>(block_data_bytes),
                           sizeof(block_data_bytes));

    // compare frozen byte data with File contents

    for (size_t i = 0; i != file.NumBlocks(); ++i) {
        ASSERT_EQ(
            block_data.substr(
                i * File::block_size,
                std::min<size_t>(File::block_size,
                                 file.virtual_block(i).size())),
            file.virtual_block(i).ToString());
    }

    // check size of Block.
    {
        File::BlockCPtr block = file.virtual_block(0).block();
        static_assert(sizeof(*block) == 16, "Block size does not match");
    }

    // read File contents using BlockReader
    {
        File::Reader fr = file.GetReader();
        ASSERT_EQ(fr.Read(8), "testtest");
        ASSERT_EQ(fr.GetVarint(), 123456u);
        ASSERT_EQ(fr.GetString(), "test1test2test3");
        ASSERT_EQ(fr.GetString(), std::string(64, '1'));
        ASSERT_EQ(fr.Get<uint16_t>(), 42);
        ASSERT_THROW(fr.Get<uint16_t>(), std::runtime_error);
    }
}

TEST(File, SerializeSomeItems) {

    // construct File with very small blocks for testing
    using File = data::FileBase<1024>;
    File file;

    using MyPair = std::pair<int, std::string>;

    // put into File some items (all of different serialization bytes)
    {
        File::Writer fw = file.GetWriter();
        fw(static_cast<unsigned>(5));
        fw(MyPair(5, "10abc"));
        fw(static_cast<double>(42.0));
        fw(std::string("test"));
    }

    //std::cout << common::hexdump(file.BlockAsString(0)) << std::endl;

    // get items back from file.
    {
        File::Reader fr = file.GetReader();
        unsigned i1 = fr.Next<unsigned>();
        ASSERT_EQ(i1, 5u);
        MyPair i2 = fr.Next<MyPair>();
        ASSERT_EQ(i2, MyPair(5, "10abc"));
        double i3 = fr.Next<double>();
        ASSERT_DOUBLE_EQ(i3, 42.0);
        std::string i4 = fr.Next<std::string>();
        ASSERT_EQ(i4, "test");
    }
}

TEST(File, SeekReadSlicesOfFiles) {
    static const bool debug = false;

    // yes, this is a prime number as block size. -tb
    static const size_t block_size = 53;

    using File = data::FileBase<block_size>;
    using VirtualBlock = File::VirtualBlock;

    // construct a small-block File with lots of items.
    File file;

    File::Writer fw = file.GetWriter();
    for (size_t i = 0; i < 1000; ++i) {
        fw(i);
    }
    fw.Close();

    ASSERT_EQ(1000u, file.NumItems());

    // read complete File
    File::Reader fr = file.GetReader();
    for (size_t i = 0; i < 1000; ++i) {
        ASSERT_TRUE(fr.HasNext());
        ASSERT_EQ(i, fr.Next<size_t>());
    }
    ASSERT_FALSE(fr.HasNext());

    // read items 95-144
    auto check_range =
        [&](size_t begin, size_t end, bool do_more = true) {
            sLOG << "Test range [" << begin << "," << end << ")";

            // seek in File to begin.
            File::Reader fr = file.GetReaderAt<size_t>(begin);

            // read the items [begin,end)
            {
                std::vector<VirtualBlock> blocks
                    = fr.GetItemBatch<size_t>(end - begin);

                using MyQueue = data::BlockQueue<block_size>;
                MyQueue queue;

                for (VirtualBlock& vb : blocks)
                    queue.AppendBlock(vb);
                queue.Close();

                MyQueue::Reader qr = queue.GetReader();

                for (size_t i = begin; i < end; ++i) {
                    ASSERT_TRUE(qr.HasNext());
                    ASSERT_EQ(i, qr.Next<size_t>());
                }
                ASSERT_FALSE(qr.HasNext());
            }

            if (!do_more) return;

            sLOG << "read more";
            static const size_t more = 100;

            // read the items [end, end + more)
            {
                std::vector<VirtualBlock> blocks
                    = fr.GetItemBatch<size_t>(more);

                using MyQueue = data::BlockQueue<block_size>;
                MyQueue queue;

                for (VirtualBlock& vb : blocks)
                    queue.AppendBlock(vb);
                queue.Close();

                MyQueue::Reader qr = queue.GetReader();

                for (size_t i = end; i < end + more; ++i) {
                    ASSERT_TRUE(qr.HasNext());
                    ASSERT_EQ(i, qr.Next<size_t>());
                }
                ASSERT_FALSE(qr.HasNext());
            }
        };

    // read some item ranges.
    for (size_t i = 90; i != 100; ++i) {
        check_range(i, 144);
    }
    for (size_t i = 140; i != 150; ++i) {
        check_range(96, i);
    }

    // some special cases.
    check_range(0, 0);
    check_range(0, 1);
    check_range(1, 2);
    check_range(990, 1000, false);
    check_range(1000, 1000, false);
}

// forced instantiation
using MyBlock = data::Block<16>;
template class data::FileBase<16>;
template class data::BlockWriterBase<16>;
template class data::BlockReader<data::FileBlockSource<16> >;

// fixed size serialization test
using MyWriter = data::BlockWriterBase<16>;
using MyReader = data::BlockReader<data::FileBlockSource<16> >;

static_assert(data::Serialization<MyWriter, int>
              ::is_fixed_size == true, "");
static_assert(data::Serialization<MyWriter, int>
              ::fixed_size == sizeof(int), "");

static_assert(data::Serialization<MyWriter, std::string>
              ::is_fixed_size == false, "");

static_assert(data::Serialization<MyWriter, std::pair<int, short> >
              ::is_fixed_size == true, "");
static_assert(data::Serialization<MyWriter, std::pair<int, short> >
              ::fixed_size == sizeof(int) + sizeof(short), "");

static_assert(data::Serialization<MyWriter, std::pair<int, std::string> >
              ::is_fixed_size == false, "");

/******************************************************************************/