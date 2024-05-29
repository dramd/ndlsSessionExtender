#include <ranges>
#include <sqlite3.h>
#include "CouchbaseLite.h"
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

        createCollation (db, "REVID", CBLCollateRevIDs);

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
