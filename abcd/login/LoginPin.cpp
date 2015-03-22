/*
 * Copyright (c) 2014, AirBitz, Inc.
 * All rights reserved.
 *
 * See the LICENSE file for more information.
 */

#include "LoginPin.hpp"
#include "Lobby.hpp"
#include "Login.hpp"
#include "LoginDir.hpp"
#include "LoginServer.hpp"
#include "../crypto/Encoding.hpp"
#include "../crypto/Random.hpp"
#include "../json/JsonBox.hpp"
#include "../json/JsonObject.hpp"
#include "../util/Json.hpp"
#include "../util/Util.hpp"

namespace abcd {

#define KEY_LENGTH 32

#define PIN_FILENAME                            "PinPackage.json"

/**
 * A round-trippable representation of the PIN-based re-login file.
 */
struct PinLocal:
    public JsonObject
{
    ABC_JSON_VALUE(pinBox,      "EMK_PINK", JsonBox)
    ABC_JSON_STRING(pinAuthId,  "DID",      nullptr)
    ABC_JSON_INTEGER(expires,   "Expires",  0)

    Status
    pinAuthIdDecode(DataChunk &result)
    {
        ABC_CHECK(pinAuthIdOk());
        ABC_CHECK(base64Decode(result, pinAuthId()));
        return Status();
    }
};

/**
 * Determines whether or not the given user can log in via PIN on this
 * device.
 */
tABC_CC ABC_LoginPinExists(const char *szUserName,
                           bool *pbExists,
                           tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    PinLocal local;
    std::string fixed;

    ABC_CHECK_NEW(Lobby::fixUsername(fixed, szUserName), pError);

    *pbExists = false;
    if (local.load(loginDirFind(fixed) + PIN_FILENAME))
        *pbExists = true;

exit:
    return cc;
}

/**
 * Deletes the local copy of the PIN-based login data.
 */
tABC_CC ABC_LoginPinDelete(const char *szUserName,
                           tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    std::string fixed;
    std::string directory;

    ABC_CHECK_NEW(Lobby::fixUsername(fixed, szUserName), pError);
    directory = loginDirFind(szUserName);
    if (!directory.empty())
        ABC_CHECK_RET(ABC_LoginDirFileDelete(directory, PIN_FILENAME, pError));

exit:
    return cc;
}

/**
 * Assuming a PIN-based login pagage exits, log the user in.
 */
tABC_CC ABC_LoginPin(std::shared_ptr<Login> &result,
                     std::shared_ptr<Lobby> lobby,
                     const char *szPin,
                     tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    std::unique_ptr<Login> login;
    CarePackage carePackage;
    tABC_LoginPackage   *pLoginPackage  = NULL;
    PinLocal local;
    char *              szEPINK         = NULL;
    DataChunk pinAuthId;
    DataChunk pinAuthKey;       // Unlocks the server
    DataChunk pinKeyKey;        // Unlocks pinKey
    DataChunk pinKey;           // Unlocks dataKey
    DataChunk dataKey;          // Unlocks the account
    JsonBox pinKeyBox;          // Holds pinKey
    std::string LPIN = lobby->username() + szPin;

    // Load the packages:
    ABC_CHECK_RET(ABC_LoginDirLoadPackages(lobby->dir(), carePackage, &pLoginPackage, pError));
    ABC_CHECK_NEW(local.load(lobby->dir() + PIN_FILENAME), pError);
    ABC_CHECK_NEW(local.pinAuthIdDecode(pinAuthId), pError);

    // Get EPINK from the server:
    ABC_CHECK_NEW(usernameSnrp().hash(pinAuthKey, LPIN), pError);
    ABC_CHECK_RET(ABC_LoginServerGetPinPackage(
        toU08Buf(pinAuthId), toU08Buf(pinAuthKey), &szEPINK, pError));
    ABC_CHECK_NEW(pinKeyBox.decode(szEPINK), pError);

    // Decrypt MK:
    ABC_CHECK_NEW(carePackage.snrp2().hash(pinKeyKey, LPIN), pError);
    ABC_CHECK_NEW(pinKeyBox.decrypt(pinKey, pinKeyKey), pError);
    ABC_CHECK_NEW(local.pinBox().decrypt(dataKey, pinKey), pError);

    // Create the Login object:
    login.reset(new Login(lobby, dataKey));
    ABC_CHECK_NEW(login->init(pLoginPackage), pError);
    result.reset(login.release());

exit:
    ABC_LoginPackageFree(pLoginPackage);
    ABC_FREE_STR(szEPINK);

    if (ABC_CC_PinExpired == cc)
    {
        tABC_Error error;
        ABC_LoginPinDelete(lobby->username().c_str(), &error);
    }

    return cc;
}

/**
 * Sets up a PIN login package, both on-disk and on the server.
 */
tABC_CC ABC_LoginPinSetup(Login &login,
                          const char *szPin,
                          time_t expires,
                          tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    CarePackage carePackage;
    tABC_LoginPackage   *pLoginPackage  = NULL;
    PinLocal local;
    AutoU08Buf          L1;
    AutoU08Buf          LP1;
    DataChunk pinAuthId;
    DataChunk pinAuthKey;       // Unlocks the server
    DataChunk pinKeyKey;        // Unlocks pinKey
    DataChunk pinKey;           // Unlocks dataKey
    JsonBox pinKeyBox;          // Holds pinKey
    JsonBox pinBox;             // Holds dataKey
    std::string LPIN = login.lobby().username() + szPin;
    std::string str;

    // Get login stuff:
    ABC_CHECK_RET(ABC_LoginDirLoadPackages(login.lobby().dir(), carePackage, &pLoginPackage, pError));
    ABC_CHECK_RET(ABC_LoginGetServerKeys(login, &L1, &LP1, pError));

    // Put dataKey in a box:
    ABC_CHECK_NEW(randomData(pinKey, KEY_LENGTH), pError);
    ABC_CHECK_NEW(pinBox.encrypt(login.dataKey(), pinKey), pError);

    // Put pinKey in a box:
    ABC_CHECK_NEW(carePackage.snrp2().hash(pinKeyKey, LPIN), pError);
    ABC_CHECK_NEW(pinKeyBox.encrypt(pinKey, pinKeyKey), pError);

    // Set up DID:
    ABC_CHECK_NEW(randomData(pinAuthId, KEY_LENGTH), pError);

    // Set up the server:
    ABC_CHECK_NEW(usernameSnrp().hash(pinAuthKey, LPIN), pError);
    ABC_CHECK_NEW(pinKeyBox.encode(str), pError);
    ABC_CHECK_RET(ABC_LoginServerUpdatePinPackage(L1, LP1,
        toU08Buf(pinAuthId), toU08Buf(pinAuthKey), str, expires, pError));

    // Save the local file:
    ABC_CHECK_NEW(local.pinBoxSet(pinBox), pError);
    ABC_CHECK_NEW(local.pinAuthIdSet(base64Encode(pinAuthId).c_str()), pError);
    ABC_CHECK_NEW(local.expiresSet(expires), pError);
    ABC_CHECK_NEW(local.save(login.lobby().dir() + PIN_FILENAME), pError);

exit:
    ABC_LoginPackageFree(pLoginPackage);

    return cc;
}

} // namespace abcd
