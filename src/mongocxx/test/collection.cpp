#include "catch.hpp"

#include <vector>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/stdx/string_view.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/collection.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/pipeline.hpp>

using namespace mongocxx;

TEST_CASE("collection copy", "[collection]") {
    client mongodb_client{uri{}};
    database db = mongodb_client["test"];

    std::string collname{"foo"};
    std::string collname2{"bar"};
    collection coll = db[collname];

    collection coll2{coll};
    collection coll3 = db[collname2];
    coll3 = coll;

    REQUIRE(coll2.name() == stdx::string_view{collname});
    REQUIRE(coll3.name() == stdx::string_view{collname});
}

TEST_CASE("collection renaming", "[collection]") {
    client mongodb_client{uri{}};
    database db = mongodb_client["test"];

    std::string collname{"mongo_cxx_driver"};
    collection coll = db[collname];
    coll.insert_one({});  // Ensure that the collection exists.

    REQUIRE(coll.name() == stdx::string_view{collname});

    std::string new_name{"mongo_cxx_newname"};
    coll.rename(new_name, true);

    REQUIRE(coll.name() == stdx::string_view{new_name});
}

TEST_CASE("CRUD functionality", "[driver::collection]") {
    client mongodb_client{uri{}};
    database db = mongodb_client["test"];
    collection coll = db["mongo_cxx_driver"];

    if (db.has_collection("mongo_cxx_driver")) coll.drop();

    SECTION("insert and read single document", "[collection]") {
        bsoncxx::builder::stream::document b;
        b << "_id" << bsoncxx::oid{bsoncxx::oid::init_tag} << "x" << 1;

        REQUIRE(coll.insert_one(b));

        bsoncxx::builder::stream::document c;
        c << "x" << 1;

        REQUIRE(coll.insert_one(c.view()));

        auto cursor = coll.find(b);

        std::size_t i = 0;
        for (auto&& x : cursor) {
            REQUIRE(x["_id"].get_oid().value == b.view()["_id"].get_oid().value);
            i++;
        }

        REQUIRE(i == 1);
    }

    SECTION("insert and read multiple documents", "[collection]") {
        bsoncxx::builder::stream::document b1;
        bsoncxx::builder::stream::document b2;
        bsoncxx::builder::stream::document b3;
        bsoncxx::builder::stream::document b4;

        b1 << "_id" << bsoncxx::oid{bsoncxx::oid::init_tag} << "x" << 1;
        b2 << "x" << 2;
        b3 << "x" << 3;
        b4 << "_id" << bsoncxx::oid{bsoncxx::oid::init_tag} << "x" << 4;

        std::vector<bsoncxx::document::view> docs{};
        docs.push_back(b1);
        docs.push_back(b2);
        docs.push_back(b3);
        docs.push_back(b4);

        auto result = coll.insert_many(docs, options::insert{});

        REQUIRE(result);
        REQUIRE(result->inserted_count() == 4);

        auto cursor = coll.find({});

        std::size_t i = 0;
        for (auto&& x : cursor) {
            i++;
            REQUIRE(x["x"].get_int32() == i);
        }

        REQUIRE(i == 4);
    }

    SECTION("insert and update single document", "[collection]") {
        using namespace bsoncxx::builder::stream;
        bsoncxx::builder::stream::document b1;
        b1 << "_id" << 1;

        coll.insert_one(b1);

        auto doc = coll.find_one(bsoncxx::document::view());
        REQUIRE(doc);
        REQUIRE(doc->view()["_id"].get_int32() == 1);

        bsoncxx::builder::stream::document update_doc;
        update_doc << "$set" << open_document << "changed" << true << close_document;

        coll.update_one(b1, update_doc);

        auto updated = coll.find_one(bsoncxx::document::view());
        REQUIRE(updated);
        REQUIRE(updated->view()["changed"].get_bool() == true);
    }

    SECTION("insert and update multiple documents", "[collection]") {
        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        coll.insert_one(b1);
        coll.insert_one(b1);

        bsoncxx::builder::stream::document b2;
        b2 << "x" << 2;

        coll.insert_one(b2);

        REQUIRE(coll.count(b1) == 2);

        bsoncxx::builder::stream::document bchanged;
        bchanged << "changed" << true;

        bsoncxx::builder::stream::document update_doc;
        update_doc << "$set" << bsoncxx::types::b_document{bchanged};

        coll.update_many(b1, update_doc);

        REQUIRE(coll.count(bchanged) == 2);
    }

    SECTION("replace document replaces only one document", "[collection]") {
        bsoncxx::builder::stream::document doc;
        doc << "x" << 1;

        coll.insert_one(doc);
        coll.insert_one(doc);

        REQUIRE(coll.count(doc) == 2);

        bsoncxx::builder::stream::document replacement;
        replacement << "x" << 2;

        coll.replace_one(doc, replacement);
        auto c = coll.count(doc);
        REQUIRE(coll.count(doc) == 1);
    }

    SECTION("non-matching upsert creates document", "[collection]") {
        using namespace bsoncxx::builder::stream;
        bsoncxx::builder::stream::document b1;
        b1 << "_id" << 1;

        bsoncxx::builder::stream::document update_doc;
        update_doc << "$set" << open_document << "changed" << true << close_document;

        options::update options;
        options.upsert(true);

        coll.update_one(b1, update_doc, options);

        auto updated = coll.find_one(bsoncxx::document::view());

        REQUIRE(updated);
        REQUIRE(updated->view()["changed"].get_bool() == true);
        REQUIRE(coll.count(bsoncxx::document::view()) == (std::int64_t)1);
    }

    SECTION("matching upsert updates document", "[collection]") {
        using namespace bsoncxx::builder::stream;
        bsoncxx::builder::stream::document b1;
        b1 << "_id" << 1;

        coll.insert_one(b1);

        bsoncxx::builder::stream::document update_doc;
        update_doc << "$set" << open_document << "changed" << true << close_document;

        options::update options;
        options.upsert(true);

        coll.update_one(b1, update_doc, options);

        auto updated = coll.find_one(bsoncxx::document::view());

        REQUIRE(updated);
        REQUIRE(updated->view()["changed"].get_bool() == true);
        REQUIRE(coll.count(bsoncxx::document::view()) == 1);
    }

    // SECTION("matching upsert updates document", "[collection]") {
    // bsoncxx::builder::stream::document b1;
    // b1 << "x" << 1;
    // model::insert_many docs{std::initializer_list<bsoncxx::document::view>{b1, b1, b1}};
    // coll.insert_many(docs);

    // coll.insert_one(bsoncxx::document::view{});
    // REQUIRE(coll.count(b1) == 3);
    // REQUIRE(coll.count() == 4);
    //}

    SECTION("document replacement", "[collection]") {
        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;
        coll.insert_one(b1);

        bsoncxx::builder::stream::document b2;
        b2 << "x" << 2;

        coll.replace_one(b1, b2);

        auto replaced = coll.find_one(b2);

        REQUIRE(replaced);
        REQUIRE(coll.count(bsoncxx::document::view()) == 1);
    }

    SECTION("filtered document delete one works", "[collection]") {
        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        coll.insert_one(b1);

        bsoncxx::builder::stream::document b2;
        b2 << "x" << 2;

        coll.insert_one(b2);
        coll.insert_one(b2);

        REQUIRE(coll.count(bsoncxx::document::view()) == 3);

        coll.delete_one(b2);

        REQUIRE(coll.count(bsoncxx::document::view()) == (std::int64_t)2);

        auto cursor = coll.find(bsoncxx::document::view());

        unsigned seen = 0;
        for (auto&& x : cursor) {
            seen |= x["x"].get_int32();
        }

        REQUIRE(seen == 3);

        coll.delete_one(b2);

        REQUIRE(coll.count(bsoncxx::document::view()) == 1);

        cursor = coll.find(bsoncxx::document::view());

        seen = 0;
        for (auto&& x : cursor) {
            seen |= x["x"].get_int32();
        }

        REQUIRE(seen == 1);

        coll.delete_one(b2);

        REQUIRE(coll.count(bsoncxx::document::view()) == 1);

        cursor = coll.find(bsoncxx::document::view());

        seen = 0;
        for (auto&& x : cursor) {
            seen |= x["x"].get_int32();
        }

        REQUIRE(seen == 1);
    }

    SECTION("delete many works", "[collection]") {
        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        coll.insert_one(b1);

        bsoncxx::builder::stream::document b2;
        b2 << "x" << 2;

        coll.insert_one(b2);
        coll.insert_one(b2);

        REQUIRE(coll.count(bsoncxx::document::view()) == 3);

        coll.delete_many(b2);

        REQUIRE(coll.count(bsoncxx::document::view()) == 1);

        auto cursor = coll.find(bsoncxx::document::view());

        unsigned seen = 0;
        for (auto&& x : cursor) {
            seen |= x["x"].get_int32();
        }

        REQUIRE(seen == 1);

        coll.delete_many(b2);

        REQUIRE(coll.count(bsoncxx::document::view()) == 1);

        cursor = coll.find(bsoncxx::document::view());

        seen = 0;
        for (auto&& x : cursor) {
            seen |= x["x"].get_int32();
        }

        REQUIRE(seen == 1);
    }

    SECTION("find works with sort", "[collection]") {
        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        bsoncxx::builder::stream::document b2;
        b2 << "x" << 2;

        bsoncxx::builder::stream::document b3;
        b3 << "x" << 3;

        coll.insert_one(b1);
        coll.insert_one(b3);
        coll.insert_one(b2);

        SECTION("sort ascending") {
            bsoncxx::builder::stream::document sort;
            sort << "x" << 1;
            options::find opts{};
            opts.sort(sort);

            auto cursor = coll.find(bsoncxx::document::view(), opts);

            std::int32_t x = 1;
            for (auto&& doc : cursor) {
                REQUIRE(x == doc["x"].get_int32());
                x++;
            }
        }

        SECTION("sort descending") {
            bsoncxx::builder::stream::document sort;
            sort << "x" << -1;
            options::find opts{};
            opts.sort(sort);

            auto cursor = coll.find(bsoncxx::document::view(), opts);

            std::int32_t x = 3;
            for (auto&& doc : cursor) {
                REQUIRE(x == doc["x"].get_int32());
                x--;
            }
        }
    }

    SECTION("find_one_and_replace works", "[collection]") {
        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        coll.insert_one(b1);
        coll.insert_one(b1);

        REQUIRE(coll.count(bsoncxx::document::view()) == 2);

        bsoncxx::builder::stream::document criteria;
        bsoncxx::builder::stream::document replacement;

        criteria << "x" << 1;
        replacement << "x" << 2;

        SECTION("without return replacement returns original") {
            auto doc = coll.find_one_and_replace(criteria, replacement);
            REQUIRE(doc->view()["x"].get_int32() == 1);
        }

        SECTION("with return replacement returns new") {
            options::find_one_and_replace options;
            options.return_document(options::return_document::k_after);
            auto doc = coll.find_one_and_replace(criteria, replacement, options);

            REQUIRE(doc->view()["x"].get_int32() == 2);
        }

        SECTION("bad criteria returns negative optional") {
            bsoncxx::builder::stream::document bad_criteria;
            bad_criteria << "x" << 3;

            auto doc = coll.find_one_and_replace(bad_criteria, replacement);

            REQUIRE(!doc);
        }
    }

    SECTION("find_one_and_update works", "[collection]") {
        using namespace bsoncxx::builder::stream;

        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        coll.insert_one(b1);
        coll.insert_one(b1);

        REQUIRE(coll.count(bsoncxx::document::view()) == 2);

        bsoncxx::builder::stream::document criteria;
        bsoncxx::builder::stream::document update;

        criteria << "x" << 1;
        update << "$set" << open_document << "x" << 2 << close_document;

        SECTION("without return update returns original") {
            auto doc = coll.find_one_and_update(criteria, update);

            REQUIRE(doc);

            REQUIRE(doc->view()["x"].get_int32() == 1);
        }

        SECTION("with return update returns new") {
            options::find_one_and_update options;
            options.return_document(options::return_document::k_after);
            auto doc = coll.find_one_and_update(criteria, update, options);

            REQUIRE(doc);
            REQUIRE(doc->view()["x"].get_int32() == 2);
        }

        SECTION("bad criteria returns negative optional") {
            bsoncxx::builder::stream::document bad_criteria;
            bad_criteria << "x" << 3;

            auto doc = coll.find_one_and_update(bad_criteria, update);

            REQUIRE(!doc);
        }
    }

    SECTION("find_one_and_delete works", "[collection]") {
        using namespace bsoncxx::builder::stream;

        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        coll.insert_one(b1);
        coll.insert_one(b1);

        REQUIRE(coll.count(bsoncxx::document::view()) == 2);

        bsoncxx::builder::stream::document criteria;

        criteria << "x" << 1;

        SECTION("delete one deletes one and returns it") {
            auto doc = coll.find_one_and_delete(criteria);

            REQUIRE(doc);

            REQUIRE(doc->view()["x"].get_int32() == 1);
            REQUIRE(coll.count(bsoncxx::document::view()) == 1);
        }
    }

    SECTION("aggregate some things", "[collection]") {
        bsoncxx::builder::stream::document b1;
        b1 << "x" << 1;

        bsoncxx::builder::stream::document b2;
        b2 << "x" << 2;

        coll.insert_one(b1);
        coll.insert_one(b2);
        coll.insert_one(b2);

        pipeline p;
        p.match(b1);

        auto results = coll.aggregate(p);
    }

    SECTION("distinct works", "[collection]") {
        using bsoncxx::builder::stream::document;

        auto distinct_cname = "distinct_coll";
        auto distinct_coll = db[distinct_cname];
        if (db.has_collection(distinct_cname)) {
            distinct_coll.drop();
        }

        document doc1{};
        doc1 << "foo" << "baz" << "garply" << 1;
        document doc2{};
        doc2 << "foo" << "bar" << "garply" << 2;
        document doc3{};
        doc3 << "foo" << "baz" << "garply" << 2;
        document doc4{};
        doc4 << "foo" << "quux" << "garply" << 9;

        bulk_write bulk{false /* unordered */};

        bulk.append(model::insert_one{doc1});
        bulk.append(model::insert_one{doc2});
        bulk.append(model::insert_one{doc3});
        bulk.append(model::insert_one{doc4});

        distinct_coll.bulk_write(bulk);

        auto distinct_results = distinct_coll.distinct("foo", {});

        // copy into a vector.
        std::vector<bsoncxx::document::value> results;
        for (auto&& result : distinct_results) {
            results.emplace_back(result);
        }

        REQUIRE(results.size() == std::size_t{1});

        auto res_doc = results[0].view();

        auto values_array = res_doc["values"].get_array().value;

        std::vector<stdx::string_view> distinct_values;
        for (auto&& value : values_array) {
            distinct_values.push_back(value.get_utf8().value);
        }

        const auto assert_contains_one = [&](stdx::string_view val) {
            REQUIRE(std::count(distinct_values.begin(), distinct_values.end(), val) == 1);
        };

        assert_contains_one("baz");
        assert_contains_one("bar");
        assert_contains_one("quux");
    }
}

TEST_CASE("create_index returns index name", "[collection]") {
    client mongodb_client{uri{}};
    database db = mongodb_client["test"];
    collection coll = db["collection"];
    coll.insert_one({});  // Ensure that the collection exists.

    bsoncxx::document::value index = bsoncxx::builder::stream::document{}
        << "a" << 1 << bsoncxx::builder::stream::finalize;

    std::string indexName{"myName"};
    options::index options{};
    options.name(indexName);

    auto response = coll.create_index(index, options);
    REQUIRE(response.view()["name"].get_utf8().value == stdx::string_view{indexName});

    bsoncxx::document::value index2 = bsoncxx::builder::stream::document{}
        << "b" << 1 << "c" << -1 << bsoncxx::builder::stream::finalize;

    auto response2 = coll.create_index(index2, options::index{});
    REQUIRE(response2.view()["name"].get_utf8().value == stdx::string_view{"b_1_c_-1"});
}
