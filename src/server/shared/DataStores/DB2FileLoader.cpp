/*
 * Copyright (C) 2011-2016 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2016 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DB2FileLoader.h"

DB2FileLoader::DB2FileLoader()
{
    data = NULL;
    fieldsOffset = NULL;
}

bool DB2FileLoader::Load(const char *filename, const char *fmt)
{
    if (data)
    {
        delete [] data;
        data = NULL;
    }

    FILE* f = fopen(filename, "rb");
    if (!f)
        return false;

    uint32 header;
    if (fread(&header, 4, 1, f) != 1)                        // Signature
    {
        fclose(f);
        return false;
    }

    EndianConvert(header);

    if (header != 0x32424457)
    {
        fclose(f);
        return false;                                       //'WDB2'
    }

    if (fread(&recordCount, 4, 1, f) != 1)                 // Number of records
    {
        fclose(f);
        return false;
    }

    EndianConvert(recordCount);

    if (fread(&fieldCount, 4, 1, f) != 1)                 // Number of fields
    {
        fclose(f);
        return false;
    }

    EndianConvert(fieldCount);

    if (fread(&recordSize, 4, 1, f) != 1)                 // Size of a record
    {
        fclose(f);
        return false;
    }

    EndianConvert(recordSize);

    if (fread(&stringSize, 4, 1, f) != 1)                 // String size
    {
        fclose(f);
        return false;
    }

    EndianConvert(stringSize);

    /* NEW WDB2 FIELDS*/
    if (fread(&tableHash, 4, 1, f) != 1)                  // Table hash
    {
        fclose(f);
        return false;
    }

    EndianConvert(tableHash);

    if (fread(&build, 4, 1, f) != 1)                     // Build
    {
        fclose(f);
        return false;
    }

    EndianConvert(build);

    if (fread(&unk1, 4, 1, f) != 1)                     // Unknown WDB2
    {
        fclose(f);
        return false;
    }

    EndianConvert(unk1);

    if (build > 12880)
    {
        if (fread(&minIndex, 4, 1, f) != 1)                           // MinIndex WDB2
        {
            fclose(f);
            return false;
        }
        EndianConvert(minIndex);

        if (fread(&maxIndex, 4, 1, f) != 1)                           // MaxIndex WDB2
        {
            fclose(f);
            return false;
        }
        EndianConvert(maxIndex);

        if (fread(&locale, 4, 1, f) != 1)                             // Locales
        {
            fclose(f);
            return false;
        }
        EndianConvert(locale);

        if (fread(&unk5, 4, 1, f) != 1)                               // Unknown WDB2
        {
            fclose(f);
            return false;
        }
        EndianConvert(unk5);
    }

    if (maxIndex != 0)
    {
        int32 diff = maxIndex - minIndex + 1;
        fseek(f, diff * 4 + diff * 2, SEEK_CUR);    // diff * 4: an index for rows, diff * 2: a memory allocation bank
    }

    fieldsOffset = new uint32[fieldCount];
    fieldsOffset[0] = 0;
    for (uint32 i = 1; i < fieldCount; i++)
    {
        fieldsOffset[i] = fieldsOffset[i - 1];
        if (fmt[i - 1] == 'b' || fmt[i - 1] == 'X')
            fieldsOffset[i] += 1;
        else
            fieldsOffset[i] += 4;
    }

    data = new unsigned char[recordSize*recordCount+stringSize];
    stringTable = data + recordSize*recordCount;

    if (fread(data, recordSize * recordCount + stringSize, 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

DB2FileLoader::~DB2FileLoader()
{
    if (data)
        delete [] data;
    if (fieldsOffset)
        delete [] fieldsOffset;
}

DB2FileLoader::Record DB2FileLoader::getRecord(size_t id)
{
    assert(data);
    return Record(*this, data + id*recordSize);
}

uint32 DB2FileLoader::GetFormatRecordSize(const char* format, int32* index_pos)
{
    uint32 recordsize = 0;
    int32 i = -1;
    for (uint32 x=0; format[x]; ++x)
    {
        switch (format[x])
        {
            case FT_FLOAT:
            case FT_INT:
                recordsize += 4;
                break;
            case FT_STRING:
                recordsize += sizeof(DbcStr);
                break;
            case FT_SORT:
                i = x;
                break;
            case FT_IND:
                i = x;
                recordsize += 4;
                break;
            case FT_BYTE:
                recordsize += 1;
                break;
        }
    }

    if (index_pos)
        *index_pos = i;

    return recordsize;
}

uint32 DB2FileLoader::GetFormatStringsFields(const char* format)
{
    uint32 stringfields = 0;
    for (uint32 x=0; format[x]; ++x)
        if (format[x] == FT_STRING)
            ++stringfields;

    return stringfields;
}

char* DB2FileLoader::AutoProduceData(const char* format, uint32& records, char**& indexTable)
{

    typedef char * ptr;
    if (strlen(format) != fieldCount)
        return NULL;

    //get struct size and index pos
    int32 i;
    uint32 recordsize=GetFormatRecordSize(format, &i);

    if (i >= 0)
    {
        uint32 maxi = 0;
        //find max index
        for (uint32 y = 0; y < recordCount; y++)
        {
            uint32 ind=getRecord(y).getUInt(i);
            if (ind>maxi)
                maxi = ind;
        }

        ++maxi;
        records = maxi;
        indexTable = new ptr[maxi];
        memset(indexTable, 0, maxi * sizeof(ptr));
    }
    else
    {
        records = recordCount;
        indexTable = new ptr[recordCount];
    }

    size_t totalSize = recordCount * recordsize;
    char* dataTable = new char[totalSize];
    memset(dataTable, 0, totalSize);

    uint32 offset=0;

    for (uint32 y =0; y < recordCount; y++)
    {
        if (i>=0)
        {
            indexTable[getRecord(y).getUInt(i)] = &dataTable[offset];
        }
        else
            indexTable[y] = &dataTable[offset];

        for (uint32 x = 0; x < fieldCount; x++)
        {
            switch (format[x])
            {
                case FT_FLOAT:
                    *((float*)(&dataTable[offset])) = getRecord(y).getFloat(x);
                    offset += sizeof(float);
                    break;
                case FT_IND:
                case FT_INT:
                    *((uint32*)(&dataTable[offset])) = getRecord(y).getUInt(x);
                    offset += sizeof(uint32);
                    break;
                case FT_BYTE:
                    *((uint8*)(&dataTable[offset])) = getRecord(y).getUInt8(x);
                    offset += sizeof(uint8);
                    break;
                case FT_STRING:
                    offset += sizeof(DbcStr);
                    break;
                case FT_NA:
                case FT_NA_BYTE:
                case FT_SORT:
                    break;
                default:
                    ASSERT(false && "Unknown field format character in DB2fmt.h");
                    break;
            }
        }
    }

    return dataTable;
}

char* DB2FileLoader::AutoProduceStrings(const char* format, char* dataTable, uint32 locale)
{
    if (strlen(format) != fieldCount)
        return NULL;

    char* stringPool= new char[stringSize];
    memcpy(stringPool, stringTable, stringSize);

    uint32 offset = 0;

    for (uint32 y =0; y < recordCount; y++)
    {
        for (uint32 x = 0; x < fieldCount; x++)
            switch (format[x])
        {
            case FT_FLOAT:
            case FT_IND:
            case FT_INT:
                offset += 4;
                break;
            case FT_BYTE:
                offset += 1;
                break;
            case FT_STRING:
            {
                DbcStr* strings = (DbcStr*)(&dataTable[offset]);
                char const* st = getRecord(y).getString(x);
                strings->m_impl[locale] = stringPool + (st - (const char*)stringTable);
                offset += sizeof(DbcStr);
                break;
            }
        }
    }

    return stringPool;
}
