// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifndef _SRC_INTERFACES_ENCLOG_HPP_
#define _SRC_INTERFACES_ENCLOG_HPP_

#include "dnmdowner.hpp"

#include <corerror.h>

namespace enc_log
{
    inline HRESULT Append(mdhandle_t metadata, mdToken token, uint32_t operation)
    {
        md_added_row_t row{ mdcursor_t{} };
        if (!md_append_row(metadata, mdtid_ENCLog, &row))
            return E_FAIL;
        if (!md_set_column_value_as_constant(row, mdtENCLog_Token, token)
            || !md_set_column_value_as_constant(row, mdtENCLog_Op, operation))
            return E_FAIL;
        return S_OK;
    }

    inline HRESULT LogToken(mdhandle_view const& handle, mdToken token, uint32_t operation = 0)
    {
        return handle.UpdateMode() == MDUpdateENC
            ? Append(handle.get(), token, operation)
            : S_OK;
    }

    inline HRESULT LogRow(mdhandle_view const& handle, mdcursor_t row, uint32_t operation = 0)
    {
        if (handle.UpdateMode() != MDUpdateENC)
            return S_OK;

        mdToken token;
        if (!md_cursor_to_token(row, &token))
            return CLDB_E_FILE_CORRUPT;
        return Append(handle.get(), token | 0x80000000u, operation);
    }
}

#endif // _SRC_INTERFACES_ENCLOG_HPP_
