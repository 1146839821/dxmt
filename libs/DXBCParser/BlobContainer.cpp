// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "DXBCUtils.h"
#include "BlobContainer.h"

namespace microsoft {

//=================================================================================================================================
// CDXBCParser

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::CDXBCParser
CDXBCParser::CDXBCParser()
{
    m_pHeader = NULL;
    m_pIndex = NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::ReadDXBC()
HRESULT CDXBCParser::ReadDXBC( const void* pContainer, UINT ContainerSizeInBytes )
{
    if (!pContainer)
    {
        return E_FAIL;
    }
    if (ContainerSizeInBytes < sizeof( DXBCHeader ))
    {
        return E_FAIL;
    }
    DXBCHeader* pHeader = (DXBCHeader*)pContainer;
    if (pHeader->ContainerSizeInBytes != ContainerSizeInBytes)
    {
        return E_FAIL;
    }
    if ((pHeader->DXBCHeaderFourCC != DXBC_FOURCC_NAME) ||
         (pHeader->Version.Major != DXBC_MAJOR_VERSION) ||
         (pHeader->Version.Minor != DXBC_MINOR_VERSION))
    {
        return E_FAIL;
    }
    // Bound the index before multiplying its count or reading any entry.
    const UINT HeaderSize = sizeof( DXBCHeader );
    if (pHeader->BlobCount > (ContainerSizeInBytes - HeaderSize) / sizeof( UINT ))
    {
        return E_FAIL;
    }
    const UINT* pIndex = (const UINT*)((const BYTE*)pHeader + HeaderSize);
    UINT NextOffset = HeaderSize + sizeof( UINT ) * pHeader->BlobCount;
    for (UINT b = 0; b < pHeader->BlobCount; b++)
    {
        // Keep the contiguous layout contract. Validate offsets as integers
        // before forming pointers, and use exclusive ends throughout.
        if (pIndex[b] != NextOffset ||
            sizeof( DXBCBlobHeader ) > ContainerSizeInBytes - NextOffset)
        {
            return E_FAIL;
        }
        const DXBCBlobHeader* pBlobHeader =
            (const DXBCBlobHeader*)((const BYTE*)pContainer + NextOffset);
        NextOffset += sizeof( DXBCBlobHeader );
        if (pBlobHeader->BlobSize > ContainerSizeInBytes - NextOffset)
        {
            return E_FAIL;
        }
        NextOffset += pBlobHeader->BlobSize;
    }

    // Ok, satisfied with integrity of container, store info.
    m_pHeader = pHeader;
    m_pIndex = pIndex;
    return S_OK;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::ReadDXBCAssumingValidSize()
HRESULT CDXBCParser::ReadDXBCAssumingValidSize( const void* pContainer )
{
    if (!pContainer)
    {
        return E_FAIL;
    }
    return ReadDXBC( (const BYTE*)pContainer, DXBCGetSizeAssumingValidPointer( (const BYTE*)pContainer ) );
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::FindNextMatchingBlob
UINT CDXBCParser::FindNextMatchingBlob( DXBCFourCC SearchFourCC, UINT SearchStartBlobIndex )
{
    if (!m_pHeader || !m_pIndex)
    {
        return (UINT)DXBC_BLOB_NOT_FOUND;
    }
    for (UINT b = SearchStartBlobIndex; b < m_pHeader->BlobCount; b++)
    {
        DXBCBlobHeader* pBlob = (DXBCBlobHeader*)((BYTE*)m_pHeader + m_pIndex[b]);
        if (pBlob->BlobFourCC == SearchFourCC)
        {
            return b;
        }
    }
    return (UINT)DXBC_BLOB_NOT_FOUND;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetVersion()
const DXBCVersion* CDXBCParser::GetVersion()
{
    return m_pHeader ? &m_pHeader->Version : NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetHash()
const DXBCHash* CDXBCParser::GetHash()
{
    return m_pHeader ? &m_pHeader->Hash : NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobCount()
UINT CDXBCParser::GetBlobCount()
{
    return m_pHeader ? m_pHeader->BlobCount : 0;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlob()
const void* CDXBCParser::GetBlob( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return NULL;
    }
    return (BYTE*)m_pHeader + m_pIndex[BlobIndex] + sizeof( DXBCBlobHeader );
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobSize()
UINT CDXBCParser::GetBlobSize( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return 0;
    }
    return ((DXBCBlobHeader*)((BYTE*)m_pHeader + m_pIndex[BlobIndex]))->BlobSize;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobFourCC()
UINT CDXBCParser::GetBlobFourCC( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return 0;
    }
    return ((DXBCBlobHeader*)((BYTE*)m_pHeader + m_pIndex[BlobIndex]))->BlobFourCC;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::RelocateBytecode()
HRESULT CDXBCParser::RelocateBytecode( UINT ByteOffset )
{
    if (!m_pHeader || !m_pIndex)
    {
        // bad -- has not been initialized yet
        return E_FAIL;
    }
    m_pHeader = (const DXBCHeader*)((const BYTE*)m_pHeader + ByteOffset);
    m_pIndex = (const UINT32*)((const BYTE*)m_pIndex + ByteOffset);
    return S_OK;
}

}