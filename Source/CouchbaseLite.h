
#pragma once
#include <JuceHeader.h>
#include <sqlite_modern_cpp.h>

namespace db {

    struct CouchbaseLiteDatabase
    {
        CouchbaseLiteDatabase (const juce::File& file);
        auto getAllDocumentIds() -> juce::StringArray;

        auto getAllDocumentIds (juce::String type) -> juce::StringArray;

        auto getDocuments (const juce::StringArray& docIds) -> juce::Array<juce::var>;
        auto getDocument (const juce::String& docId) -> juce::var;

        auto getLocalDocument (const juce::String& docId) -> juce::var;
        auto setLocalDocument (juce::var doc) -> int;

        auto getAttachments (const juce::var& doc) -> juce::StringArray;
        auto getAttachment (const juce::var& doc, const juce::String& attachmentId) -> juce::File;
        auto getAttachmentMime (const juce::var& doc, const juce::String& attachmentId) -> juce::String;
    private:
        juce::File dbFile;
        sqlite::database db;
        JUCE_LEAK_DETECTOR (CouchbaseLiteDatabase)
    };
}
