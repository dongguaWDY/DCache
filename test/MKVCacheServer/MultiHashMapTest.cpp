/**
* Tencent is pleased to support the open source community by making DCache available.
* Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
* Licensed under the BSD 3-Clause License (the "License"); you may not use this file
* except in compliance with the License. You may obtain a copy of the License at
*
* https://opensource.org/licenses/BSD-3-Clause
*
* Unless required by applicable law or agreed to in writing, software distributed under
* the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
* either express or implied. See the License for the specific language governing permissions
* and limitations under the License.
*/

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdlib.h>

#include <MKCacheGlobe.h>
#include <MKHash.h>
#include <MKCacheComm.h>
#include <MKCacheServer.h>

using namespace DCache;
using namespace tars;
using namespace std;

#define LOCATION __FUNCTION__<<", "<<__LINE__<<": "


//这里固定存储在multiHashMap的数据结构，如下:
// 主key名字：          mkey
// 第一个ukey名字： vkeyInt
// 第二个ukey名字： vkeyString
// 第一个value名字：valueInt
// 第二个value名字：valueString
MultiHashMap multiHashMap;


bool initMulitiHashMap()
{
    const key_t shmKey = 930346523; // 共享内存的key
    const size_t jmemNum = 2; // 设置两个jmem块
    const string contentOfShm = "1G";
    const size_t shmSize = TC_Common::toSize(contentOfShm, 1024 * 1024 * 1024);  // 共享内存的大小设为1G
    const uint8_t keyType = 0; // 先测hash类型的，值为0表示hash类型，1:set类型，2:zset类型，3:list类型
    MKHash *pHash = new MKHash();

    // 创建共享内存
    int shmgetRet;
    if ((shmgetRet = shmget(shmKey, 0, 0)) != -1 || errno != ENOENT)
    {
        cout << LOCATION << "create shared memory fail, key: " << shmKey << " has been used, may be a conflict key; shmget() return  " << shmgetRet << endl;
        // return false;
    }
    
    multiHashMap.init(jmemNum);
    multiHashMap.initMainKeySize(0); // 该值可设可不设 
    multiHashMap.initDataSize(100);
    multiHashMap.initLock(shmKey, jmemNum, -1);


    multiHashMap.initStore(shmKey, shmSize, keyType);

    // multiHashMap.setSyncTime(TC_Common::strto<unsigned int>(_tcConf["/Main/Cache<SyncTime>"]));
    // multiHashMap.setToDoFunctor(&_todoFunctor);


    typedef uint32_t(MKHash::*TpMem)(const string &);
    TC_Multi_HashMap_Malloc::hash_functor cmd_mk(pHash, static_cast<TpMem>(&MKHash::HashMK));
    TC_Multi_HashMap_Malloc::hash_functor cmd_mkuk(pHash, static_cast<TpMem>(&MKHash::HashMKUK));
    multiHashMap.setHashFunctorM(cmd_mk);
    multiHashMap.setHashFunctor(cmd_mkuk);
    multiHashMap.setAutoErase(false);

    return true;
    
}


class MultiHashMapTest : public ::testing::Test
{
protected:
    void SetUp() override
    {

        
        string _cacheConf =
        R"(<Main>
            <Cache>
                #指定共享内存使用的key
                ShmKey=12345
                #内存的大小
                ShmSize=1G
                #平均数据size，用于初始化共享内存空间
                AvgDataSize=128
                #设置hash比率(设置chunk数据块/hash项比值)
                HashRadio=2

                #内存块个数
                JmemNum=10

            </Cache>
            
            <Record>
                # 主key字段名字
                MKey=mkey
                UKey=vkeyInt|vkeyString
                VKey=valueInt|valueString

                <Field>
                    mkey = 0|string|require||255
                    vkeyInt = 1|int|require|0|0
                    vkeyString = 2|string|require||255
                    valueInt = 3|int|require|0|0
                    valueString = 4|string|require||65535
                </Field>
            </Record>
        </Main>)";

        _tcConf.parseString(_cacheConf);

        map<string, int> mTypeLenInDB;
        mTypeLenInDB["byte"] = 1;
        mTypeLenInDB["short"] = 4;
        mTypeLenInDB["int"] = 4;
        mTypeLenInDB["long"] = 8;
        mTypeLenInDB["float"] = 4;
        mTypeLenInDB["double"] = 8;
        mTypeLenInDB["unsigned int"] = 4;
        mTypeLenInDB["unsigned short"] = 4;
        mTypeLenInDB["string"] = 0;
        
        //初始化字段信息   
        fieldConfig.sMKeyName = _tcConf["/Main/Record<MKey>"];
        fieldConfig.mpFieldType[fieldConfig.sMKeyName] = 0;
        vector<string> vtUKeyName = TC_Common::sepstr<string>(_tcConf["/Main/Record<UKey>"], "|");
        for (size_t i = 0; i < vtUKeyName.size(); i++)
        {
            string ukeyName = TC_Common::trim(vtUKeyName[i]);
            fieldConfig.vtUKeyName.push_back(ukeyName);
            fieldConfig.mpFieldType[ukeyName] = 1;
        }
        vector<string> vtValueName = TC_Common::sepstr<string>(_tcConf["/Main/Record<VKey>"], "|");
        for (size_t i = 0; i < vtValueName.size(); i++)
        {
            string valueName = TC_Common::trim(vtValueName[i]);
            fieldConfig.vtValueName.push_back(valueName);
            fieldConfig.mpFieldType[valueName] = 2;
        }
        map<string, string> mpFiedInfo = _tcConf.getDomainMap("/Main/Record/Field");
        for (map<string, string>::iterator it = mpFiedInfo.begin(); it != mpFiedInfo.end(); it++)
        {
            vector<string> vtInfo = TC_Common::sepstr<string>(it->second, "|", true);
            struct FieldInfo info;
            info.tag = TC_Common::strto<uint32_t>(TC_Common::trim(vtInfo[0]));
            info.type = TC_Common::trim(vtInfo[1]);
            info.bRequire = (TC_Common::trim(vtInfo[2]) == "require") ? true : false;
            info.defValue = TC_Common::trim(vtInfo[3]);
            info.lengthInDB = mTypeLenInDB[info.type];
            if (vtInfo.size() >= 5)
            {
                info.lengthInDB = TC_Common::strto<int>(vtInfo[4]);
            }
            fieldConfig.mpFieldInfo[it->first] = info;
        }

        g_app.gstat()->setFieldConfig(fieldConfig);
        createOneRecord();
        decodeRecord();


        _mkeyMaxDataCount = 0;
        _deleteDirty = true; 
        _insertAtHead = true; 
        _updateInOrder = true;
        dirty = false;
        expireTimeSecond = 0;
        ver = 0;
    
        
    }

    // 人为造一条数据
    void createOneRecord()
    {
    	mainKey = "messi";
        UpdateValue uv;
        uv.op = SET;
        int i = 666;
        
        uv.value = TC_Common::tostr(i);
        mpValue["vkeyInt"] = uv;
        
        uv.value = "ukeystring" + TC_Common::tostr(i);
        mpValue["vkeyString"] = uv;
        
        uv.value = TC_Common::tostr(i);
        mpValue["valueInt"] = uv;
        
        uv.value = "valuestring" + TC_Common::tostr(i);
        mpValue["valueString"] = uv;

    }

    // 在写入multiHashMap前需要对数据进行编码
    void decodeRecord()
    {
        map<string, DCache::UpdateValue> mpUK;
        map<string, DCache::UpdateValue> mpJValue;

        int iRetCode;
        ASSERT_TRUE(checkSetValue(mpValue, mpUK, mpJValue, iRetCode)) <<  LOCATION << "checkSetValue() ERROR, return " << iRetCode << endl;

            
        // 对所有ukey编码
        TarsEncode uKeyEncode;
        for (size_t i = 0; i < fieldConfig.vtUKeyName.size(); i++)
        {
            const string &sUKName = fieldConfig.vtUKeyName[i];
            const FieldInfo &fieldInfo = fieldConfig.mpFieldInfo[sUKName];
            uKeyEncode.write(mpUK[sUKName].value, fieldInfo.tag, fieldInfo.type);
        }
        uk.assign(uKeyEncode.getBuffer(), uKeyEncode.getLength());

        
        MultiHashMap::Value vData;

        
        // 对所有value编码
        TarsEncode valueEncode;
        for (size_t i = 0; i < fieldConfig.vtValueName.size(); i++)
        {
            const string &sValueName = fieldConfig.vtValueName[i];
            const FieldInfo &fieldInfo = fieldConfig.mpFieldInfo[sValueName];
            valueEncode.write(mpJValue[sValueName].value, fieldInfo.tag, fieldInfo.type);
        }
        value.assign(valueEncode.getBuffer(), valueEncode.getLength());
    }
    
protected:    
    FieldConf fieldConfig;
    TC_Config _tcConf;

    size_t _mkeyMaxDataCount; // 某个主key下最大限数据量,=0时无限制，只有在大于0且不读db时有效
    bool _deleteDirty; // 限制主key下最大限数据量功能开启时，是否删除脏数据
    bool _insertAtHead; // 和底层实现有关
    bool _updateInOrder;
    bool dirty;
    uint32_t expireTimeSecond;
    uint8_t ver;
    uint32_t syncTime;
    
    string mainKey;
    map<std::string, DCache::UpdateValue> mpValue;
    string uk;
    string value;

    int ret;
    MultiHashMap::Value values;
};


// 写数据
TEST_F(MultiHashMapTest, insertMKV)
{

    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

}

// 查找数据
TEST_F(MultiHashMapTest, getOneRecord)
{

    ret = multiHashMap.get(mainKey, uk, values);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.get error, return " << ret << endl;

    ret = multiHashMap.get(mainKey, uk, values, syncTime, expireTimeSecond, ver, dirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.get error, return " << ret << endl;
    cout << LOCATION << "syncTime|expireTimeSecond|ver|dirty: " << syncTime << "|" << expireTimeSecond << "|" << unsigned(ver) << "|" << dirty << endl;
    // 查找不存在的数据
    ret = multiHashMap.get("NOTEXIST1", uk, values);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_NO_DATA) << "multiHashMap.get error, return " << ret << endl;

    // 查找不存在的数据
    ret = multiHashMap.get(mainKey, "NOTEXIST1", values);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_NO_DATA) << "multiHashMap.get error, return " << ret << endl;
}


// 删除数据
TEST_F(MultiHashMapTest, delMKV)
{
    // 删除一条已经存在数据
    ret = multiHashMap.delSetBit(mainKey, uk, time(NULL));
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.delSetBit error, return " << ret << endl;

    // 删除一条已经被删除的数据
    ret = multiHashMap.delSetBit(mainKey, uk, time(NULL));
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DATA_DEL) << "multiHashMap.delSetBit error, return " << ret << endl;

    
    // 查找数据
    MultiHashMap::Value values;
    ret = multiHashMap.get(mainKey, uk, values);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DATA_DEL) << "multiHashMap.get error, return " << ret << endl;

    // 删除一条本不存在的数据
    ret = multiHashMap.delSetBit("NOTEXIST", uk, time(NULL));
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_NO_DATA) << "multiHashMap.delSetBit error, return " << ret << endl;

    //TODO: 删除一条onleyKey数据，验证返回值是否是TC_Multi_HashMap_Malloc::RT_ONLY_KEY

}





// 插入一条设置了过期时间的数据，然后验证过期功能是否生效

TEST_F(MultiHashMapTest, dataExpired)
{


    expireTimeSecond = TC_TimeProvider::getInstance()->getNow() + 3; // 3秒后过期
    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ret = multiHashMap.get(mainKey, uk, values, true, TC_TimeProvider::getInstance()->getNow());
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.get error, return " << ret << endl;

    sleep(2); // 睡眠2秒，数据应该仍存在
    ret = multiHashMap.get(mainKey, uk, values, true, TC_TimeProvider::getInstance()->getNow());
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.get error, return " << ret << endl;

    sleep(2); // 再睡眠2秒，数据应该过期了
    ret = multiHashMap.get(mainKey, uk, values, true, TC_TimeProvider::getInstance()->getNow());
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DATA_EXPIRED) << "multiHashMap.get error, return " << ret << endl;
}



// onlyKey
TEST_F(MultiHashMapTest, onlyKey)
{
    cout << expireTimeSecond << endl;
    
    multiHashMap.clear();
	 
    // 插入一条数据
    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    // 检查mainKey是否存在
    ret = multiHashMap.checkMainKey(mainKey);
//    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK);  // TODO: 这里为什么返回TC_Multi_HashMap_Malloc::RT_PART_DATA(主key存在，里面的数据可能不完整)

    // 设置为onlyKey
    ret = multiHashMap.set(mainKey);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DATA_EXIST) << "multiHashMap.set error, return " << ret << endl;

    mainKey = "NOTEXIST";
    ret = multiHashMap.set(mainKey);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ret = multiHashMap.checkMainKey(mainKey);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_ONLY_KEY);

    // 如何删除onleyKey数据
    // ret = multiHashMap.delReal(mainKey, uk);
    // ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_ONLY_KEY) << "multiHashMap.delSetBit error, return " << ret << endl;


}


// 1.验证checkDirty、setDirty、count、getTotalElementCount和dirtyCount
TEST_F(MultiHashMapTest, dirtyData)
{

    
    // 插入一条非脏数据
    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    // 验证是否是脏数据
    ret = multiHashMap.checkDirty(mainKey, uk);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK);

    // 设置数据为脏数据
    ret = multiHashMap.setDirty(mainKey, uk);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK);

    // 计算脏数据条数
    size_t cntOfDirtyData = 0;
    cntOfDirtyData = multiHashMap.dirtyCount();
    ASSERT_EQ(1, cntOfDirtyData);

    // 再次验证是否是脏数据
    ret = multiHashMap.checkDirty(mainKey, uk);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DIRTY_DATA);

    // 设置数据为干净数据
    ret = multiHashMap.setClean(mainKey, uk);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK);

    // 再次验证是否是脏数据
    ret = multiHashMap.checkDirty(mainKey, uk);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK);

    cntOfDirtyData = multiHashMap.dirtyCount();
    ASSERT_EQ(0, cntOfDirtyData);

    // 清除所有数据
    multiHashMap.clear();
    ASSERT_EQ(0, multiHashMap.count(mainKey));

    // 插入若干条脏数据
    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ret = multiHashMap.set(mainKey, uk + "1", value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, true, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ret = multiHashMap.set(mainKey, uk + "2", value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ASSERT_EQ(3, multiHashMap.count(mainKey));

    // 插入另一个mainKey
    string anotherMainKey = mainKey + "another";
    
    ret = multiHashMap.set(anotherMainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, true, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ret = multiHashMap.set(anotherMainKey, uk + "1", value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ret = multiHashMap.set(anotherMainKey, uk + "2", value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, true, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    ASSERT_EQ(3, multiHashMap.count(anotherMainKey));

    // 计算数据总条数
    ASSERT_EQ(6, multiHashMap.getTotalElementCount());

    // 计算脏数据元素个数
    cntOfDirtyData = multiHashMap.dirtyCount();
    ASSERT_EQ(3, cntOfDirtyData);

    // 清除所有数据
    multiHashMap.clear();
    

}




// 数据版本号相关
TEST_F(MultiHashMapTest, dataVersion)
{

    // 插入一条数据(ver = 0表示写入时不关注版本号，但是当数据成功写入后，再次获取版本号，其值是2)
    cout << "default data version = " << unsigned(ver) << endl;
    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    // 获取数据版本号
    ret = multiHashMap.get(mainKey, uk, values, syncTime, expireTimeSecond, ver, dirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.get error, return " << ret << endl;
    cout << "data version = " << unsigned(ver) << endl;

    // 带版本号写入数据(版本号不一致，无法写入)
    ver = (uint8_t)4;
    ret = multiHashMap.set(mainKey, uk, value + "diffValue", expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DATA_VER_MISMATCH);

    // 带版本号删除数据(版本号不一致，无法删除)
    ret = multiHashMap.delSetBit(mainKey, uk, ver, time(NULL));
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DATA_VER_MISMATCH) << "multiHashMap.delSetBit error, return " << ret << endl;

    

    // 获取数据版本号(此时版本号仍为2)
    ret = multiHashMap.get(mainKey, uk, values, syncTime, expireTimeSecond, ver, dirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.get error, return " << ret << endl;
    ASSERT_EQ(ver, 2);
    

    // 版本号一致则成功写入，并且版本号自增1
    ver = (uint8_t)2;
    ret = multiHashMap.set(mainKey, uk, value + "diffValue", expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK);

    // 获取数据版本号
    ret = multiHashMap.get(mainKey, uk, values, syncTime, expireTimeSecond, ver, dirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.get error, return " << ret << endl;
    ASSERT_EQ(ver, 3);

    // 带版本号删除数据(版本号一致，删除成功)
    ver = (uint8_t)3;
    ret = multiHashMap.delSetBit(mainKey, uk, ver, time(NULL));
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.delSetBit error, return " << ret << endl;

    ret = multiHashMap.get(mainKey, uk, values, syncTime, expireTimeSecond, ver, dirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_DATA_DEL) << "multiHashMap.get error, return " << ret << endl;


    // 清除所有数据
    multiHashMap.clear();   

}



// 测试其余接口
TEST_F(MultiHashMapTest, miscellany)
{


    ASSERT_FALSE(multiHashMap.isReadOnly());
    
    multiHashMap.setReadOnly(true);
    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_READONLY) << "multiHashMap.set error, return " << ret << endl;

    multiHashMap.setReadOnly(false);
    ret = multiHashMap.set(mainKey, uk, value, expireTimeSecond, ver, TC_Multi_HashMap_Malloc::DELETE_FALSE, dirty, TC_Multi_HashMap_Malloc::AUTO_DATA, _insertAtHead, _updateInOrder, _mkeyMaxDataCount, _deleteDirty);
    ASSERT_EQ(ret, TC_Multi_HashMap_Malloc::RT_OK) << "multiHashMap.set error, return " << ret << endl;

    
    // 清除所有数据
    multiHashMap.clear();   

}







int main(int argc, char **argv)
{
    //ASSERT_TRUE(initMulitiHashMap());
    assert(initMulitiHashMap());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}



