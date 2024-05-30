#include <ranges>
#include <sqlite3.h>
#include "CouchbaseLite.h"
#include "nlohmann/json.hpp"

#include <string_view>
#include <charconv>
#include <sstream>
#include <algorithm>
#include <compare>

// @echolox: Note sure about this for your version of Clang, I needed it back in the day
#ifndef _MSC_VER

std::strong_ordering cmp_icase(unsigned char x, unsigned char y)
{
    return std::toupper(x) <=> std::toupper(y);
};

namespace std
{
    template<class I1, class I2, class Cmp>
    constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2, Cmp comp)
        -> decltype(comp(*f1, *f2))
    {
        using ret_t = decltype(comp(*f1, *f2));
        static_assert(std::disjunction_v<
                          std::is_same<ret_t, std::strong_ordering>,
                          std::is_same<ret_t, std::weak_ordering>,
                          std::is_same<ret_t, std::partial_ordering>>,
                      "The return type must be a comparison category type.");
     
        bool exhaust1 = (f1 == l1);
        bool exhaust2 = (f2 == l2);
        for (; !exhaust1 && !exhaust2; exhaust1 = (++f1 == l1), exhaust2 = (++f2 == l2))
            if (auto c = comp(*f1, *f2); c != 0)
                return c;
     
        return !exhaust1 ? std::strong_ordering::greater:
               !exhaust2 ? std::strong_ordering::less:
                           std::strong_ordering::equal;
    }
}
#endif

namespace db {
    static inline auto sgn (int n) -> int
    {
        return n > 0 ? 1 : (n < 0 ? -1 : 0);
    }

    static auto defaultCollate (const char* str1, int len1, const char* str2, int len2) -> int
    {
        int result = memcmp (str1, str2, std::min (len1, len2));
        return sgn (result ? result : (len1 - len2));
    }

    static auto parseDigits (const char* str, const char* end) -> unsigned
    {
        unsigned result = 0;
        for (; str < end; ++str)
        {
            if (!isdigit (*str))
                return 0;
            result = 10 * result + int((*str) - '0');
        }
        return result;
    }


    static int32_t defaultCollate( std::string_view rev1, std::string_view rev2 ) 
    {
        return defaultCollate(rev1.data(), rev1.length(), rev2.data(), rev2.length());
    }

    int32_t collateRevisions( void* data, int rev1_len, const void* rev1_data, int rev2_len, const void* rev2_data )
    {
        auto rev1 = std::string_view( ( const char* ) rev1_data, rev1_len );
        auto rev2 = std::string_view( ( const char* ) rev2_data, rev2_len );

        auto dash1 = rev1.find( '-' );
        auto dash2 = rev2.find( '-' );

        if ( ( dash1 == 1 && dash2 == 1 )
            || dash1 > 8 || dash2 > 8
            || dash1 == std::string_view::npos || dash2 == std::string_view::npos )
        {
            return defaultCollate( rev1, rev2 );
        }

        auto gen1_str = rev1.substr( 0, dash1 );
        auto gen2_str = rev2.substr( 0, dash2 );

        int gen1, gen2;
        try
        {
            gen1 = std::stoi( gen1_str.data( ) );
            gen2 = std::stoi( gen2_str.data( ) );
        }
        catch ( std::exception& e )
        {
            auto error = e.what( );

            return defaultCollate( rev1, rev2 );
        }

        if ( gen1 == 0 || gen2 == 0 )
        {
            return defaultCollate( rev1, rev2 );
        }

        const auto difference = gen1 - gen2;
        const auto result = difference > 0 ? 1 : ( difference < 0 ? -1 : 0 );
        if ( result != 0 )
        {
            return result;
        }

        auto suffix1 = dash1 + 1;
        auto suffix2 = dash2 + 1;

        if ( rev1.size( ) > suffix1 && rev2.size( ) > suffix2 )
        {
            // Compare suffixes:
            return defaultCollate( rev1.substr( suffix1 ), rev2.substr( suffix2 ) );
        }

        // Invalid format, fall back to compare as plain text:
        return defaultCollate( rev1, rev2 );
    }

    template<typename Numeric>
    static int32_t threeWayCompare(const Numeric& a, const Numeric& b)
    {
        const auto difference = a - b;
        return difference > 0 ? 1 : ( difference < 0 ? -1 : 0 );
    }

    int32_t collateJSON( void* data, int js1_len, const void* js1_data, int js2_len, const void* js2_data )
    {
        auto js1 = std::string_view( ( const char* ) js1_data, js1_len );
        auto js2 = std::string_view( ( const char* ) js2_data, js2_len );

        int64_t i1,i2;
#ifdef _MSC_VER
        auto r1 = std::from_chars( js1.data( ), js1.data( ) + js1.size( ), i1 );
        auto r2 = std::from_chars( js2.data( ), js2.data( ) + js2.size( ), i2 );
#else
        auto r1 = std::from_chars<int64_t>(js1.begin(), js1.end(), i1);
        auto r2 = std::from_chars<int64_t>(js2.begin(), js2.end(), i2);
#endif

        auto no_error = std::errc();
        
        if (r1.ec == no_error && r2.ec == no_error)
        {
            // We got integers!
            return threeWayCompare( i1, i2 );
        }

        // If they aren't integers let's try strings.

        if ( js1.starts_with( "\"" ) )
        {
            const auto cmp = std::lexicographical_compare_three_way( js1.begin( ), js1.end( ), js2.begin( ), js2.end( ) );
            return ( cmp < 0 ) ? -1 : ( ( cmp == 0 ) ? 0 : 1 );
        }
        
        // The disappointments continue: Clang doesn't have the std::from_chars overload for double or float...
        // So we won't handle floats and doubles for now....

        const nlohmann::json d1 = nlohmann::json::parse(js1);
        const nlohmann::json d2 = nlohmann::json::parse(js2);

        bool canCompare = d1 && d2;

        if ( canCompare && (d1.type( ) == d2.type( ) ) )
        {
            if ( d1.is_number_integer() )
            {
                return threeWayCompare( std::int64_t(d1), std::int64_t(d2) );
            }
            else if ( d1.is_number_unsigned( ) )
            {
                return threeWayCompare( std::uint64_t( d1 ), std::uint64_t( d2 ) );
            }
            else if ( d1.is_number_float( ) )
            {
                return threeWayCompare( double(d1), double(d2) );
            }
            else if ( d1.is_boolean( ) )
            {
                return threeWayCompare( bool(d1), bool(d2) );
            }
            else if ( d1.is_array( ) )
            {
                assert( false ); // Not yet implemented
            }
        }

        assert( false ); // wtf is this data?
        return threeWayCompare( js1.size( ), js2.size( ) );
    }


    /* A proper revision ID consists of a generation number, a hyphen, and an arbitrary suffix.
       Compare the generation numbers numerically, and then the suffixes lexicographically.
       If either string isn't a proper rev ID, fall back to lexicographic comparison. */
    auto CBLCollateRevIDs (void* context,
                           int len1,
                           const void* chars1,
                           int len2,
                           const void* chars2) -> int
    {
        const char* rev1 = static_cast<const char*> (chars1);
        const char* rev2 = static_cast<const char*> (chars2);

        const char* dash1 = static_cast<const char*> (memchr (rev1, '-', len1));
        const char* dash2 = static_cast<const char*> (memchr (rev2, '-', len2));

        if ((dash1 == rev1 + 1 && dash2 == rev2 + 1)
            || dash1 > rev1 + 8 || dash2 > rev2 + 8
            || dash1 == nullptr || dash2 == nullptr)
        {
            // Single-digit generation #s, or improper rev IDs; just compare as plain text:
            return defaultCollate (rev1, len1, rev2, len2);
        }
        // Parse generation numbers. If either is invalid, revert to default collation:
        int gen1 = parseDigits (rev1, dash1);
        int gen2 = parseDigits (rev2, dash2);
        if (!gen1 || !gen2)
            return defaultCollate (rev1, len1, rev2, len2);

        // Compare generation numbers; if they match, compare suffixes:
        int result = sgn (gen1 - gen2);
        return result ? result : defaultCollate (dash1 + 1, len1 - (int)(dash1 + 1 - rev1), dash2 + 1, len2 - (int)(dash2 + 1 - rev2));
    }

    auto getViewTableCreate (const int id) -> juce::String
    {
        return juce::String ("CREATE TABLE IF NOT EXISTS 'maps_") + juce::String (id) + juce::String ("' (sequence INTEGER NOT NULL REFERENCES revs(sequence) ON DELETE CASCADE, key TEXT NOT NULL COLLATE JSON, value TEXT, fulltext_id INTEGER, bbox_id INTEGER, geokey BLOB)");
    }

    auto getDatabaseFile (const juce::File& file) -> juce::File
    {
        if (file.isDirectory())
        {
            return file.getChildFile("db.sqlite3");
        }
        return file;
    }

    auto getFilePath (const juce::File& file) -> std::string
    {
        return getDatabaseFile (file).getFullPathName().toStdString();
    }

    auto createCollation (sqlite::database& db, juce::StringRef collationName, int (*xCompare) (void*, int, const void*, int, const void*), void* pArgs = nullptr) -> int
    {
        return sqlite3_create_collation (db.connection().get(), collationName, SQLITE_UTF8, pArgs, xCompare);
    }

    CouchbaseLiteDatabase::CouchbaseLiteDatabase (const juce::File& file) : dbFile (getDatabaseFile (file)), db (getFilePath (file))
    {
        db << "CREATE TABLE IF NOT EXISTS docs (doc_id INTEGER PRIMARY KEY, docid TEXT UNIQUE NOT NULL, expiry_timestamp INTEGER)";
        db << "CREATE TABLE IF NOT EXISTS info (key TEXT PRIMARY KEY, value TEXT)";
        db << "CREATE TABLE IF NOT EXISTS localdocs (docid TEXT UNIQUE NOT NULL, revid TEXT NOT NULL COLLATE REVID, json BLOB)";
        db << "CREATE TABLE IF NOT EXISTS revs (sequence INTEGER PRIMARY KEY AUTOINCREMENT, doc_id INTEGER NOT NULL REFERENCES docs(doc_id) ON DELETE CASCADE, revid TEXT NOT NULL COLLATE REVID, parent INTEGER REFERENCES revs(sequence) ON DELETE SET NULL, current BOOLEAN, deleted BOOLEAN DEFAULT 0, json BLOB, no_attachments BOOLEAN, doc_type TEXT, UNIQUE (doc_id, revid))";
        db << "CREATE TABLE IF NOT EXISTS views (view_id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, version TEXT, lastsequence INTEGER DEFAULT 0, total_docs INTEGER DEFAULT -1)";

        /*sqlite3_create_collation(dbHandle, "JSON", SQLITE_UTF8,
                                 kCBLCollateJSON_Unicode, CBLCollateJSON);
        sqlite3_create_collation(dbHandle, "JSON_RAW", SQLITE_UTF8,
                                 kCBLCollateJSON_Raw, CBLCollateJSON);
        sqlite3_create_collation(dbHandle, "JSON_ASCII", SQLITE_UTF8,
                                 kCBLCollateJSON_ASCII, CBLCollateJSON);*/
        int result;
        result = createCollation (db, "REVID", CBLCollateRevIDs);
        assert( result == SQLITE_OK );
        result = createCollation (db, "JSON", db::collateJSON);
        assert( result == SQLITE_OK );

        db << "SELECT view_id FROM views;" >> [&] (const int id)
        {
            db << getViewTableCreate (id).toStdString();
        };
    }

    auto CouchbaseLiteDatabase::getAllDocumentIds() -> juce::StringArray
    {
        juce::StringArray docIds;

        db << "SELECT doc_id, docid FROM docs" >> [&] (const int doc_id, const std::string docId)
        {
            docIds.addIfNotAlreadyThere (docId);
        };

        return docIds;
    }

    auto CouchbaseLiteDatabase::getAllDocumentIds (juce::String type) -> juce::StringArray
    {
        juce::StringArray docIds;

        db << "SELECT doc_id FROM revs WHERE doc_type = (?) AND current = 1" << type.toStdString() >> [&] (const int doc_id)
        {
            db << "SELECT docid from docs WHERE doc_id = (?)" << doc_id >> [&] (const std::string docId)
            {
                docIds.addIfNotAlreadyThere (juce::String (docId));
            };
        };

        return docIds;
    }

    static const juce::String localDocIdPrefix = "_local/";

    auto CouchbaseLiteDatabase::getLocalDocument (const juce::String& docId) -> juce::var
    {
        const juce::String localDocId = localDocIdPrefix + docId;
        
        juce::var document;
        db << "SELECT docid, revid, json FROM localdocs WHERE docid = (?)" << localDocId.toStdString() >> [&](const std::string docId, const std::string revId, const std::string json) {
            document = juce::JSON::parse (json);
            if (auto obj = document.getDynamicObject())
            {
                obj->setProperty ("_rev", juce::String (revId));
                obj->setProperty ("_id", juce::String (docId).replaceFirstOccurrenceOf(localDocIdPrefix, ""));
            }
        };
        return document;
    }

    std::vector<char> toSqliteBlob(const std::string& string)
    {
        return { string.begin(), string.end() };
    }

    auto CouchbaseLiteDatabase::setLocalDocument (juce::var document) -> int
    {
        if(document.hasProperty("_id") && document.hasProperty("_rev"))
        {
            juce::String docId = localDocIdPrefix + document["_id"].toString();
            juce::String revId = document["_rev"];
            if (auto obj = document.getDynamicObject())
            {
                obj->removeProperty("_id");
                obj->removeProperty("_rev");
            }

            auto jsonBlob = toSqliteBlob(juce::JSON::toString(document, true).toStdString());
            db << "INSERT INTO localdocs (docid, revid, json) values (?,?,?) ON CONFLICT DO UPDATE SET json=? WHERE docid=?" << docId.toStdString() << revId.toStdString() << jsonBlob << jsonBlob << docId.toStdString();
            
            int const rows_modified = db.rows_modified();
            DBG(db.rows_modified() << " Rows Modified");
            return rows_modified;
        }
        else
        {
            jassertfalse;
            return 0;
        }
    }

    auto CouchbaseLiteDatabase::getDocuments (const juce::StringArray& docIds) -> juce::Array<juce::var>
    {
        juce::Array<juce::var> results;

        for (auto& docId : docIds)
        {
            auto doc = getDocument (docId);
            if (doc.isObject())
            {
                results.add (doc);
            }
        }

        return results;
    }

    auto CouchbaseLiteDatabase::getDocument (const juce::String& docId) -> juce::var
    {
        juce::var document;

        db << "SELECT doc_id, docid FROM docs WHERE docid = (?)" << docId.toStdString() >> [&] (const int doc_id, const std::string docId)
        {
            db << "SELECT doc_id, revid, json, doc_type FROM revs WHERE doc_id = (?) AND current = 1" << doc_id >> [&] (const int doc_id, const std::string revId, const std::string json, const std::string type)
            {
                document = juce::JSON::parse (json);
                jassert (document.isObject());
                if (auto obj = document.getDynamicObject())
                {
                    if (!type.empty())
                        obj->setProperty ("type", juce::String (type));
                    obj->setProperty ("_rev", juce::String (revId));
                    obj->setProperty ("_id", juce::String (docId));
                }
            };
        };

        return document;
    }

    auto CouchbaseLiteDatabase::getAttachments (const juce::var& doc) -> juce::StringArray
    {
        juce::StringArray names;
        if (doc.hasProperty ("_attachments") && doc.isObject())
        {
            juce::DynamicObject::Ptr obj = doc["_attachments"].getDynamicObject();
            auto nvs = obj->getProperties();
            for (const int i : std::views::iota (0, nvs.size()))
            {
                names.addIfNotAlreadyThere (nvs.getName (i).toString());
            }
        }

        return names;
    }

    auto CouchbaseLiteDatabase::getAttachmentMime (const juce::var& doc, const juce::String& attachmentId) -> juce::String
    {
        if (doc.hasProperty ("_attachments") && doc["_attachments"].hasProperty (attachmentId))
        {
            auto attachmentDoc = doc["_attachments"][juce::Identifier (attachmentId)];
            return attachmentDoc["content_type"].toString();
        }
        return {};
    }

    auto CouchbaseLiteDatabase::getAttachment (const juce::var& doc, const juce::String& attachmentId) -> juce::File
    {
        if (doc.hasProperty ("_attachments") && doc["_attachments"].hasProperty (attachmentId))
        {
            auto attachmentDoc = doc["_attachments"][juce::Identifier (attachmentId)];
            if (attachmentDoc["stub"])
            {
                auto digest = attachmentDoc["digest"].toString();
                auto algo = digest.upToFirstOccurrenceOf ("-", false, true);
                if (algo != "sha1")
                {
                    jassertfalse;
                }
                auto base64 = digest.fromFirstOccurrenceOf ("-", false, true);
                juce::MemoryOutputStream stream;
                juce::Base64::convertFromBase64 (stream, base64);
                juce::MemoryBlock block = stream.getMemoryBlock();
                auto hexString = juce::String::toHexString (block.getData(), static_cast<int> (block.getSize()), 0);

                auto attDir = dbFile.getSiblingFile ("attachments");
                if (attDir.isDirectory())
                {
                    auto path = attDir.getChildFile (hexString.toUpperCase() + ".blob");
                    if (path.existsAsFile())
                    {
                        return path;
                    }
                    else
                    {
                        DBG ("Attachment file not found");
                        jassertfalse;
                    }
                }
                else
                {
                    DBG ("Attachments dir does not exist");
                    jassertfalse;
                }
            }
            else
            {
                DBG ("Attachment is not a stub");
                jassertfalse;
            }
        }
        else
        {
            DBG ("Attachment does not exist");
        }

        return {};
    }

}
