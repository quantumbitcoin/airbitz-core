/*
 * Copyright (c) 2014, AirBitz, Inc.
 * All rights reserved.
 *
 * See the LICENSE file for more information.
 */

#include "LoginPackages.hpp"
#include "../crypto/Encoding.hpp"
#include "../json/JsonBox.hpp"
#include "../util/Json.hpp"
#include "../util/Util.hpp"

namespace abcd {

// LoginPackage.json:
#define JSON_ACCT_EMK_LP2_FIELD                 "EMK_LP2"
#define JSON_ACCT_EMK_LRA3_FIELD                "EMK_LRA3"
#define JSON_ACCT_ESYNCKEY_FIELD                "ESyncKey"
#define JSON_ACCT_ELP1_FIELD                    "ELP1"
#define JSON_ACCT_ELRA1_FIELD                   "ELRA1"

#define ABC_CHECK_JSON(f) ABC_CHECK_ASSERT(0 == (f), ABC_CC_JSONError, "JSON error");

/**
 * Frees a LoginPackage object and all its members.
 */
void ABC_LoginPackageFree(tABC_LoginPackage *pSelf)
{
    if (pSelf)
    {
        if (pSelf->EMK_LP2)  json_decref(pSelf->EMK_LP2);
        if (pSelf->EMK_LRA3) json_decref(pSelf->EMK_LRA3);
        if (pSelf->ESyncKey) json_decref(pSelf->ESyncKey);
        if (pSelf->ELP1)     json_decref(pSelf->ELP1);
        if (pSelf->ELRA1)    json_decref(pSelf->ELRA1);

        ABC_CLEAR_FREE(pSelf, sizeof(tABC_LoginPackage));
    }
}

/**
 * Loads a LoginPackage object from a string.
 */
tABC_CC ABC_LoginPackageDecode(tABC_LoginPackage **ppSelf,
                               char *szLoginPackage,
                               tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    tABC_LoginPackage *pSelf = NULL;
    json_t  *pJSON_Root     = NULL;
    int     e;

    // Allocate self:
    ABC_NEW(pSelf, tABC_LoginPackage);

    // Parse the JSON:
    json_error_t error;
    pJSON_Root = json_loads(szLoginPackage, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing LoginPackage JSON");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing LoginPackage JSON");

    // Unpack the contents:
    e = json_unpack(pJSON_Root, "{s?o, s?o, s:o, s?o, s?o}",
                    JSON_ACCT_EMK_LP2_FIELD,     &pSelf->EMK_LP2,
                    JSON_ACCT_EMK_LRA3_FIELD,    &pSelf->EMK_LRA3,
                    JSON_ACCT_ESYNCKEY_FIELD,    &pSelf->ESyncKey,
                    JSON_ACCT_ELP1_FIELD,        &pSelf->ELP1,
                    JSON_ACCT_ELRA1_FIELD,       &pSelf->ELRA1);
    ABC_CHECK_SYS(!e, "Error parsing LoginPackage JSON");

    // Save everything:
    if (pSelf->EMK_LP2)     json_incref(pSelf->EMK_LP2);
    if (pSelf->EMK_LRA3)    json_incref(pSelf->EMK_LRA3);
    if (pSelf->ESyncKey)    json_incref(pSelf->ESyncKey);
    if (pSelf->ELP1)        json_incref(pSelf->ELP1);
    if (pSelf->ELRA1)       json_incref(pSelf->ELRA1);
    *ppSelf = pSelf;
    pSelf = NULL;

exit:
    if (pSelf)          ABC_LoginPackageFree(pSelf);
    if (pJSON_Root)     json_decref(pJSON_Root);

    return cc;
}

/**
 * Serializes a LoginPackage object to a JSON string.
 */
tABC_CC ABC_LoginPackageEncode(tABC_LoginPackage *pSelf,
                               char **pszLoginPackage,
                               tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    json_t  *pJSON_Root     = NULL;

    // Build the main body:
    pJSON_Root = json_object();
    ABC_CHECK_NULL(pJSON_Root);

    // Write master key:
    if (pSelf->EMK_LP2)
    {
        ABC_CHECK_JSON(json_object_set(pJSON_Root, JSON_ACCT_EMK_LP2_FIELD, pSelf->EMK_LP2));
    }
    if (pSelf->EMK_LRA3)
    {
        ABC_CHECK_JSON(json_object_set(pJSON_Root, JSON_ACCT_EMK_LRA3_FIELD, pSelf->EMK_LRA3));
    }

    // Write sync key:
    ABC_CHECK_JSON(json_object_set(pJSON_Root, JSON_ACCT_ESYNCKEY_FIELD, pSelf->ESyncKey));

    // Write server keys:
    if (pSelf->ELP1)
    {
        ABC_CHECK_JSON(json_object_set(pJSON_Root, JSON_ACCT_ELP1_FIELD, pSelf->ELP1));
    }
    if (pSelf->ELRA1)
    {
        ABC_CHECK_JSON(json_object_set(pJSON_Root, JSON_ACCT_ELRA1_FIELD, pSelf->ELRA1));
    }

    // Write out:
    *pszLoginPackage = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER);
    ABC_CHECK_NULL(*pszLoginPackage);

exit:
    if (pJSON_Root)     json_decref(pJSON_Root);

    return cc;
}

/**
 * Decrypts and hex-encodes the SyncKey stored in the login package.
 */
tABC_CC ABC_LoginPackageGetSyncKey(tABC_LoginPackage *pSelf,
                                   const tABC_U08Buf MK,
                                   char **pszSyncKey,
                                   tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    DataChunk syncKey;

    JsonBox box(pSelf->ESyncKey);
    ABC_CHECK_NEW(box.decrypt(syncKey, MK), pError);
    ABC_STRDUP(*pszSyncKey, base16Encode(syncKey).c_str());

exit:
    return cc;
}

} // namespace abcd
