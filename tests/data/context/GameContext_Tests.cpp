#include "CppUnitTest.h"

#include "data\context\GameContext.hh"

#include "services\AchievementRuntime.hh"

#include "data\models\AchievementModel.hh"

#include "tests\RA_UnitTestHelpers.h"
#include "tests\data\DataAsserts.hh"

#include "tests\mocks\MockAudioSystem.hh"
#include "tests\mocks\MockEmulatorContext.hh"
#include "tests\mocks\MockClock.hh"
#include "tests\mocks\MockConfiguration.hh"
#include "tests\mocks\MockDesktop.hh"
#include "tests\mocks\MockLocalStorage.hh"
#include "tests\mocks\MockOverlayManager.hh"
#include "tests\mocks\MockServer.hh"
#include "tests\mocks\MockSessionTracker.hh"
#include "tests\mocks\MockThreadPool.hh"
#include "tests\mocks\MockUserContext.hh"
#include "tests\mocks\MockWindowManager.hh"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using ra::services::StorageItemType;

namespace ra {
namespace data {
namespace context {
namespace tests {

TEST_CLASS(GameContext_Tests)
{
private:
    static void RemoveFirstLine(std::string& sString)
    {
        const auto nIndex = sString.find('\n');
        if (nIndex == std::string::npos)
            sString.clear();
        else
            sString.erase(0, nIndex + 1);
    }

public:
    class GameContextHarness : public GameContext
    {
    public:
        GameContextHarness() noexcept :
            m_OverrideGameContext(this),
            m_OverrideRuntime(&runtime)
        {
        }

        ra::api::mocks::MockServer mockServer;
        ra::services::mocks::MockClock mockClock;
        ra::services::mocks::MockConfiguration mockConfiguration;
        ra::services::mocks::MockLocalStorage mockStorage;
        ra::services::mocks::MockThreadPool mockThreadPool;
        ra::services::mocks::MockAudioSystem mockAudioSystem;
        ra::ui::viewmodels::mocks::MockOverlayManager mockOverlayManager;
        ra::data::context::mocks::MockEmulatorContext mockEmulator;
        ra::data::context::mocks::MockSessionTracker mockSessionTracker;
        ra::data::context::mocks::MockUserContext mockUser;
        ra::services::AchievementRuntime runtime;
        ra::ui::viewmodels::mocks::MockWindowManager mockWindowManager;

        void SetGameId(unsigned int nGameId) noexcept { m_nGameId = nGameId; }

        void SetRichPresenceFromFile(bool bValue) noexcept { m_bRichPresenceFromFile = bValue; }

        Achievement& MockAchievement()
        {
            auto& pAch = NewAchievement(Achievement::Category::Core);
            pAch.SetID(1U);
            pAch.SetTitle("AchievementTitle");
            pAch.SetDescription("AchievementDescription");
            pAch.SetBadgeImage("12345");
            pAch.SetPoints(5);
            pAch.SetActive(true);
            return pAch;
        }

        RA_Leaderboard& MockLeaderboard()
        {
            auto& pLeaderboard = *m_vLeaderboards.emplace_back(std::make_unique<RA_Leaderboard>(1U));
            pLeaderboard.SetTitle("LeaderboardTitle");
            pLeaderboard.SetDescription("LeaderboardDescription");
            return pLeaderboard;
        }

        unsigned int GetCodeNoteSize(ra::ByteAddress nAddress)
        {
            const auto pIter = m_mCodeNotes.find(nAddress);
            if (pIter == m_mCodeNotes.end())
                return 0U;

            return pIter->second.Bytes;
        }

        void RemoveNonAchievementAssets()
        {
            for (gsl::index nIndex = Assets().Count() - 1; nIndex >= 0; nIndex--)
            {
                auto* pAsset = Assets().GetItemAt(nIndex);
                if (!pAsset || pAsset->GetType() != ra::data::models::AssetType::Achievement)
                    Assets().RemoveAt(nIndex);
            }
        }

    private:
        ra::services::ServiceLocator::ServiceOverride<ra::data::context::GameContext> m_OverrideGameContext;
        ra::services::ServiceLocator::ServiceOverride<ra::services::AchievementRuntime> m_OverrideRuntime;
    };

    class GameContextNotifyTarget : public GameContext::NotifyTarget
    {
    public:
        bool GetActiveGameChanged() const noexcept { return m_bActiveGameChanged; }

        const std::wstring* GetNewCodeNote(ra::ByteAddress nAddress)
        {
            const auto pIter = m_vCodeNotesChanged.find(nAddress);
            if (pIter != m_vCodeNotesChanged.end())
                return &pIter->second;

            return nullptr;
        }

    protected:
        void OnActiveGameChanged() noexcept override
        {
            m_bActiveGameChanged = true;
            m_vCodeNotesChanged.clear();
        }

        void OnCodeNoteChanged(ra::ByteAddress nAddress, const std::wstring& sNewNote) override
        {
            m_vCodeNotesChanged.insert_or_assign(nAddress, sNewNote);
        }

    private:
        bool m_bActiveGameChanged = false;
        std::map<ra::ByteAddress, std::wstring> m_vCodeNotesChanged;
    };

    TEST_METHOD(TestLoadGameTitle)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request& request, ra::api::FetchGameData::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);

            response.Title = L"Game";
            return true;
        });

        game.LoadGame(1U);

        Assert::AreEqual(1U, game.GameId());
        Assert::AreEqual(ra::data::context::GameContext::Mode::Normal, game.GetMode());
        Assert::AreEqual(std::wstring(L"Game"), game.GameTitle());
    }

    TEST_METHOD(TestLoadGamePopup)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request& request, ra::api::FetchGameData::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);

            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.Title = "Ach1";
            ach1.Description = "Desc1";
            ach1.Author = "Auth1";
            ach1.BadgeName = "12345";
            ach1.CategoryId = 3;
            ach1.Created = 1234567890;
            ach1.Updated = 1234599999;
            ach1.Definition = "1=1";
            ach1.Points = 5;

            response.Title = L"GameTitle";
            response.ImageIcon = "9743";
            return true;
        });

        game.LoadGame(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(std::wstring(L"Overlay\\info.wav")));

        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Loaded GameTitle"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"1 achievements, 5 points"), pPopup->GetDescription());
        Assert::AreEqual(std::string("9743"), pPopup->GetImage().Name());
    }

    TEST_METHOD(TestLoadGameNotify)
    {
        class NotifyHarness : public GameContext::NotifyTarget
        {
        public:
            bool m_bNotified = false;

        protected:
            void OnActiveGameChanged() noexcept override { m_bNotified = true; }
        };
        NotifyHarness notifyHarness;

        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            response.Title = L"GameTitle";
            response.ImageIcon = "9743";
            return true;
        });

        game.AddNotifyTarget(notifyHarness);
        game.LoadGame(0U);

        Assert::AreEqual(0U, game.GameId());
        Assert::IsFalse(notifyHarness.m_bNotified);

        game.LoadGame(1U);
        Assert::AreEqual(1U, game.GameId());
        Assert::IsTrue(notifyHarness.m_bNotified);

        notifyHarness.m_bNotified = false;
        game.LoadGame(2U);
        Assert::AreEqual(2U, game.GameId());
        Assert::IsTrue(notifyHarness.m_bNotified);

        notifyHarness.m_bNotified = false;
        game.LoadGame(0U);
        Assert::AreEqual(0U, game.GameId());
        Assert::IsTrue(notifyHarness.m_bNotified);

        notifyHarness.m_bNotified = false;
        game.RemoveNotifyTarget(notifyHarness);
        game.LoadGame(2U);
        Assert::AreEqual(2U, game.GameId());
        Assert::IsFalse(notifyHarness.m_bNotified);
    }

    TEST_METHOD(TestLoadGameRichPresence)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            response.RichPresence = "Display:\nHello, World";
            return true;
        });

        game.LoadGame(1U);

        Assert::IsTrue(game.HasRichPresence());
        Assert::AreEqual(std::string("Display:\nHello, World"),
            game.mockStorage.GetStoredData(ra::services::StorageItemType::RichPresence, L"1"));
        Assert::IsFalse(game.IsRichPresenceFromFile());
    }

    TEST_METHOD(TestLoadGameResetsRichPresenceFromFile)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            response.RichPresence = "Display:\nHello, World";
            return true;
        });

        game.SetRichPresenceFromFile(true);
        game.LoadGame(1U);

        Assert::IsTrue(game.HasRichPresence());
        Assert::AreEqual(std::string("Display:\nHello, World"),
            game.mockStorage.GetStoredData(ra::services::StorageItemType::RichPresence, L"1"));
        Assert::IsFalse(game.IsRichPresenceFromFile());
    }

    TEST_METHOD(TestLoadGameAchievements)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.Title = "Ach1";
            ach1.Description = "Desc1";
            ach1.Author = "Auth1";
            ach1.BadgeName = "12345";
            ach1.CategoryId = 3;
            ach1.Created = 1234567890;
            ach1.Updated = 1234599999;
            ach1.Definition = "1=1";
            ach1.Points = 5;

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 7;
            ach2.Title = "Ach2";
            ach2.Description = "Desc2";
            ach2.Author = "Auth2";
            ach2.BadgeName = "12345";
            ach2.CategoryId = 5;
            ach2.Created = 1234567890;
            ach2.Updated = 1234599999;
            ach2.Definition = "1=1";
            ach2.Points = 15;
            return true;
        });

        game.LoadGame(1U);

        const auto* pAch1 = game.FindAchievement(5U);
        Assert::IsNotNull(pAch1);
        Ensures(pAch1 != nullptr);
        Assert::AreEqual(std::string("Ach1"), pAch1->Title());
        Assert::AreEqual(std::string("Desc1"), pAch1->Description());
        Assert::AreEqual(std::string("Auth1"), pAch1->Author());
        Assert::AreEqual(std::string("12345"), pAch1->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Core, pAch1->GetCategory());
        Assert::AreEqual(5U, pAch1->Points());
        Assert::AreEqual(std::string("1=1"), pAch1->GetTrigger());

        const auto* pAch2 = game.FindAchievement(7U);
        Assert::IsNotNull(pAch2);
        Ensures(pAch2 != nullptr);
        Assert::AreEqual(std::string("Ach2"), pAch2->Title());
        Assert::AreEqual(std::string("Desc2"), pAch2->Description());
        Assert::AreEqual(std::string("Auth2"), pAch2->Author());
        Assert::AreEqual(std::string("12345"), pAch2->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Unofficial, pAch2->GetCategory());
        Assert::AreEqual(15U, pAch2->Points());
        Assert::AreEqual(std::string("1=1"), pAch2->GetTrigger());

        game.RemoveNonAchievementAssets();
        Assert::AreEqual({ 2U }, game.Assets().Count());

        const auto* vmAch1 = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(0));
        Assert::IsNotNull(vmAch1);
        Ensures(vmAch1 != nullptr);
        Assert::AreEqual(5U, vmAch1->GetID());
        Assert::AreEqual(std::wstring(L"Ach1"), vmAch1->GetName());
        Assert::AreEqual(std::wstring(L"Desc1"), vmAch1->GetDescription());
        Assert::AreEqual(std::wstring(L"Auth1"), vmAch1->GetAuthor());
        Assert::AreEqual(ra::data::models::AssetCategory::Core, vmAch1->GetCategory());
        Assert::AreEqual(5, vmAch1->GetPoints());
        Assert::AreEqual(std::wstring(L"12345"), vmAch1->GetBadge());
        Assert::AreEqual(std::string("1=1"), vmAch1->GetTrigger());
        Assert::IsFalse(vmAch1->IsModified());

        const auto* vmAch2 = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(1));
        Assert::IsNotNull(vmAch2);
        Ensures(vmAch2 != nullptr);
        Assert::AreEqual(7U, vmAch2->GetID());
        Assert::AreEqual(std::wstring(L"Ach2"), vmAch2->GetName());
        Assert::AreEqual(std::wstring(L"Desc2"), vmAch2->GetDescription());
        Assert::AreEqual(std::wstring(L"Auth2"), vmAch2->GetAuthor());
        Assert::AreEqual(ra::data::models::AssetCategory::Unofficial, vmAch2->GetCategory());
        Assert::AreEqual(15, vmAch2->GetPoints());
        Assert::AreEqual(std::wstring(L"12345"), vmAch2->GetBadge());
        Assert::AreEqual(std::string("1=1"), vmAch2->GetTrigger());
        Assert::IsFalse(vmAch2->IsModified());
    }

    TEST_METHOD(TestLoadGameInvalidAchievementFlags)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.Title = "Ach1";
            ach1.Description = "Desc1";
            ach1.Author = "Auth1";
            ach1.BadgeName = "12345";
            ach1.CategoryId = 0; // not a valid category
            ach1.Created = 1234567890;
            ach1.Updated = 1234599999;
            ach1.Definition = "1=1";
            ach1.Points = 5;

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 7;
            ach2.Title = "Ach2";
            ach2.Description = "Desc2";
            ach2.Author = "Auth2";
            ach2.BadgeName = "12345";
            ach2.CategoryId = 5;
            ach2.Created = 1234567890;
            ach2.Updated = 1234599999;
            ach2.Definition = "1=1";
            ach2.Points = 15;
            return true;
        });

        game.LoadGame(1U);

        const auto* pAch1 = game.FindAchievement(5U);
        Assert::IsNull(pAch1);

        const auto* pAch2 = game.FindAchievement(7U);
        Assert::IsNotNull(pAch2);
        Ensures(pAch2 != nullptr);
        Assert::AreEqual(std::string("Ach2"), pAch2->Title());
        Assert::AreEqual(std::string("Desc2"), pAch2->Description());
        Assert::AreEqual(std::string("Auth2"), pAch2->Author());
        Assert::AreEqual(std::string("12345"), pAch2->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Unofficial, pAch2->GetCategory());
        Assert::AreEqual(15U, pAch2->Points());
        Assert::AreEqual(std::string("1=1"), pAch2->GetTrigger());

        game.RemoveNonAchievementAssets();
        Assert::AreEqual({ 1U }, game.Assets().Count());

        const auto* vmAch2 = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(0));
        Assert::IsNotNull(vmAch2);
        Ensures(vmAch2 != nullptr);
        Assert::AreEqual(7U, vmAch2->GetID());
        Assert::AreEqual(std::wstring(L"Ach2"), vmAch2->GetName());
        Assert::AreEqual(std::wstring(L"Desc2"), vmAch2->GetDescription());
        Assert::AreEqual(ra::data::models::AssetCategory::Unofficial, vmAch2->GetCategory());
        Assert::AreEqual(15, vmAch2->GetPoints());
        Assert::AreEqual(std::wstring(L"12345"), vmAch2->GetBadge());
        Assert::AreEqual(std::string("1=1"), vmAch2->GetTrigger());
        Assert::IsFalse(vmAch2->IsModified());
    }

    TEST_METHOD(TestLoadGameMergeLocalAchievements)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.Title = "Ach1";
            ach1.Description = "Desc1";
            ach1.Author = "Auth1";
            ach1.BadgeName = "12345";
            ach1.CategoryId = 3;
            ach1.Created = 1234567890;
            ach1.Updated = 1234599999;
            ach1.Definition = "1=1";
            ach1.Points = 5;

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 7;
            ach2.Title = "Ach2";
            ach2.Description = "Desc2";
            ach2.Author = "Auth2";
            ach2.BadgeName = "12345";
            ach2.CategoryId = 5;
            ach2.Created = 1234567890;
            ach2.Updated = 1234599999;
            ach2.Definition = "1=1";
            ach2.Points = 15;
            return true;
        });

        game.mockStorage.MockStoredData(ra::services::StorageItemType::UserAchievements, L"1",
            "Version\n"
            "Game\n"
            "7:1=2:Ach2b:Desc2b::::Auth2b:25:1234554321:1234555555:::54321\n"
            "0:\"1=1\":\"Ach3\":\"Desc3\"::::Auth3:20:1234511111:1234500000:::555\n"
            "0:R:1=1:Ach4:Desc4::::Auth4:10:1234511111:1234500000:::556\n"
        );

        game.LoadGame(1U);

        auto* pAch = game.FindAchievement(5U);
        Assert::IsNotNull(pAch);
        Ensures(pAch != nullptr);
        Assert::AreEqual(std::string("Ach1"), pAch->Title());
        Assert::AreEqual(std::string("Desc1"), pAch->Description());
        Assert::AreEqual(std::string("Auth1"), pAch->Author());
        Assert::AreEqual(std::string("12345"), pAch->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Core, pAch->GetCategory());
        Assert::AreEqual(5U, pAch->Points());
        Assert::AreEqual(std::string("1=1"), pAch->GetTrigger());

        // local achievement data for 7 should be merged with server achievement data
        pAch = game.FindAchievement(7U);
        Assert::IsNotNull(pAch);
        Assert::AreEqual(std::string("Ach2b"), pAch->Title());
        Assert::AreEqual(std::string("Desc2b"), pAch->Description());
        Assert::AreEqual(std::string("Auth2"), pAch->Author()); // author not merged
        Assert::AreEqual(std::string("54321"), pAch->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Unofficial, pAch->GetCategory()); // category not merged
        Assert::AreEqual(25U, pAch->Points());
        Assert::AreEqual(std::string("1=2"), pAch->GetTrigger());

        // no server achievement, assign FirstLocalId
        pAch = game.FindAchievement(GameAssets::FirstLocalId);
        Assert::IsNotNull(pAch);
        Assert::AreEqual(std::string("Ach3"), pAch->Title());
        Assert::AreEqual(std::string("Desc3"), pAch->Description());
        Assert::AreEqual(std::string("Auth3"), pAch->Author());
        Assert::AreEqual(std::string("00555"), pAch->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Local, pAch->GetCategory());
        Assert::AreEqual(20U, pAch->Points());
        Assert::AreEqual(std::string("1=1"), pAch->GetTrigger());

        // no server achievement, assign next local id
        pAch = game.FindAchievement(GameAssets::FirstLocalId + 1);
        Assert::IsNotNull(pAch);
        Assert::AreEqual(std::string("Ach4"), pAch->Title());
        Assert::AreEqual(std::string("Desc4"), pAch->Description());
        Assert::AreEqual(std::string("Auth4"), pAch->Author());
        Assert::AreEqual(std::string("00556"), pAch->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Local, pAch->GetCategory());
        Assert::AreEqual(10U, pAch->Points());
        Assert::AreEqual(std::string("R:1=1"), pAch->GetTrigger());
    }

    TEST_METHOD(TestLoadGameMergeLocalAchievementsWithIds)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response&)
        {
            return true;
        });

        game.mockStorage.MockStoredData(ra::services::StorageItemType::UserAchievements, L"1",
            "Version\n"
            "Game\n"
            "7:1=2:Ach2b:Desc2b::::Auth2b:25:1234554321:1234555555:::54321\n"
            "999000001:1=1:Ach3:Desc3::::Auth3:20:1234511111:1234500000:::555\n"
            "999000003:1=1:Ach4:Desc4::::Auth4:10:1234511111:1234500000:::556\n"
        );

        game.LoadGame(1U);

        // 7 is not a known ID for this game, it should be loaded into a local achievement (we'll check later)
        auto* pAch = game.FindAchievement(7U);
        Assert::IsNotNull(pAch);
        Ensures(pAch != nullptr);
        Assert::AreEqual(std::string("Ach2b"), pAch->Title());
        Assert::AreEqual(std::string("Desc2b"), pAch->Description());
        Assert::AreEqual(std::string("Auth2b"), pAch->Author());
        Assert::AreEqual(std::string("54321"), pAch->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Local, pAch->GetCategory());
        Assert::AreEqual(25U, pAch->Points());
        Assert::AreEqual(std::string("1=2"), pAch->GetTrigger());

        // explicit ID should be recognized
        pAch = game.FindAchievement(999000001U);
        Assert::IsNotNull(pAch);
        Ensures(pAch != nullptr);
        Assert::AreEqual(std::string("Ach3"), pAch->Title());
        Assert::AreEqual(std::string("Desc3"), pAch->Description());
        Assert::AreEqual(std::string("Auth3"), pAch->Author());
        Assert::AreEqual(std::string("00555"), pAch->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Local, pAch->GetCategory());
        Assert::AreEqual(20U, pAch->Points());
        Assert::AreEqual(std::string("1=1"), pAch->GetTrigger());

        // explicit ID should be recognized
        pAch = game.FindAchievement(999000003U);
        Assert::IsNotNull(pAch);
        Ensures(pAch != nullptr);
        Assert::AreEqual(std::string("Ach4"), pAch->Title());
        Assert::AreEqual(std::string("Desc4"), pAch->Description());
        Assert::AreEqual(std::string("Auth4"), pAch->Author());
        Assert::AreEqual(std::string("00556"), pAch->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Local, pAch->GetCategory());
        Assert::AreEqual(10U, pAch->Points());
        Assert::AreEqual(std::string("1=1"), pAch->GetTrigger());


        game.RemoveNonAchievementAssets();
        Assert::AreEqual({ 3U }, game.Assets().Count());

        // 7 is not a known ID for this game, it should be loaded into a local achievement
        const auto* vmAch = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(0));
        Assert::IsNotNull(vmAch);
        Ensures(vmAch != nullptr);
        Assert::AreEqual(7U, vmAch->GetID());
        Assert::AreEqual(std::wstring(L"Ach2b"), vmAch->GetName());
        Assert::AreEqual(std::wstring(L"Desc2b"), vmAch->GetDescription());
        Assert::AreEqual(ra::data::models::AssetCategory::Local, vmAch->GetCategory());
        Assert::AreEqual(25, vmAch->GetPoints());
        Assert::AreEqual(std::wstring(L"54321"), vmAch->GetBadge());
        Assert::AreEqual(std::string("1=2"), vmAch->GetTrigger());
        Assert::IsFalse(vmAch->IsModified());
        Assert::AreEqual(ra::data::models::AssetChanges::Unpublished, vmAch->GetChanges());

        // explicit ID should be honored
        vmAch = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(1));
        Assert::IsNotNull(vmAch);
        Ensures(vmAch != nullptr);
        Assert::AreEqual(999000001U, vmAch->GetID()); // non-vms get first id and first id + 1
        Assert::AreEqual(std::wstring(L"Ach3"), vmAch->GetName());
        Assert::AreEqual(std::wstring(L"Desc3"), vmAch->GetDescription());
        Assert::AreEqual(ra::data::models::AssetCategory::Local, vmAch->GetCategory());
        Assert::AreEqual(20, vmAch->GetPoints());
        Assert::AreEqual(std::wstring(L"00555"), vmAch->GetBadge());
        Assert::AreEqual(std::string("1=1"), vmAch->GetTrigger());
        Assert::IsFalse(vmAch->IsModified());
        Assert::AreEqual(ra::data::models::AssetChanges::Unpublished, vmAch->GetChanges());

        // explicit ID should be honored
        vmAch = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(2));
        Assert::IsNotNull(vmAch);
        Ensures(vmAch != nullptr);
        Assert::AreEqual(999000003U, vmAch->GetID()); // non-vms get first id and first id + 1
        Assert::AreEqual(std::wstring(L"Ach4"), vmAch->GetName());
        Assert::AreEqual(std::wstring(L"Desc4"), vmAch->GetDescription());
        Assert::AreEqual(ra::data::models::AssetCategory::Local, vmAch->GetCategory());
        Assert::AreEqual(10, vmAch->GetPoints());
        Assert::AreEqual(std::wstring(L"00556"), vmAch->GetBadge());
        Assert::AreEqual(std::string("1=1"), vmAch->GetTrigger());
        Assert::IsFalse(vmAch->IsModified());
        Assert::AreEqual(ra::data::models::AssetChanges::Unpublished, vmAch->GetChanges());


        // new achievement should be allocated an ID higher than the largest existing local
        // ID, even if intermediate values are available
        const auto& pAch2 = game.NewAchievement(Achievement::Category::Local);
        Assert::AreEqual(999000004U, pAch2.ID());
    }

    TEST_METHOD(TestReloadAchievementLocal)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response&)
        {
            return true;
        });

        game.mockStorage.MockStoredData(ra::services::StorageItemType::UserAchievements, L"1",
            "Version\n"
            "Game\n"
            "7:1=2:Ach2b:Desc2b::::Auth2b:25:1234554321:1234555555:::54321\n"
            "999000001:1=1:Ach3:Desc3::::Auth3:20:1234511111:1234500000:::555\n"
            "999000003:1=1:Ach4:Desc4::::Auth4:10:1234511111:1234500000:::556\n"
        );

        game.LoadGame(1U);

        // 7 is not a known ID for this game, it should be loaded into a local achievement, so it can be reloaded from the file
        auto* pAch = game.FindAchievement(7U);
        Assert::IsNotNull(pAch);
        Ensures(pAch != nullptr);
        Assert::AreEqual(std::string("Ach2b"), pAch->Title());

        pAch->SetTitle("Ach2c");
        Assert::AreEqual(std::string("Ach2c"), pAch->Title());
        Assert::IsTrue(game.ReloadAchievement(7U));
        Assert::AreEqual(std::string("Ach2b"), pAch->Title());

        // explicit local ID should be reloaded successfully
        pAch = game.FindAchievement(999000003U);
        Assert::IsNotNull(pAch);
        Ensures(pAch != nullptr);
        Assert::AreEqual(std::string("Desc4"), pAch->Description());

        pAch->SetDescription("Desc4b");
        Assert::AreEqual(std::string("Desc4b"), pAch->Description());
        Assert::IsTrue(game.ReloadAchievement(999000003U));
        Assert::AreEqual(std::string("Desc4"), pAch->Description());

        // unknown ID cannot be found in file - achievement should not be updated or eliminated
        pAch = game.FindAchievement(999000001U);
        Assert::IsNotNull(pAch);
        Ensures(pAch != nullptr);
        Assert::AreEqual(std::string("Desc3"), pAch->Description());
        pAch->SetID(1234U);

        pAch->SetDescription("Desc3b");
        Assert::AreEqual(std::string("Desc3b"), pAch->Description());
        Assert::IsFalse(game.ReloadAchievement(pAch->ID()));
        Assert::AreEqual(std::string("Desc3b"), pAch->Description());

        pAch = game.FindAchievement(1234U);
        Assert::IsNotNull(pAch);
    }

    TEST_METHOD(TestLoadGameLeaderboards)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& lb1 = response.Leaderboards.emplace_back();
            lb1.Id = 7U;
            lb1.Title = "LB1";
            lb1.Description = "Desc1";
            lb1.Definition = "STA:1=1::CAN:1=1::SUB:1=1::VAL:1";
            lb1.Format = "SECS";

            auto& lb2 = response.Leaderboards.emplace_back();
            lb2.Id = 8U;
            lb2.Title = "LB2";
            lb2.Description = "Desc2";
            lb2.Definition = "STA:1=1::CAN:1=1::SUB:1=1::VAL:1";
            lb2.Format = "FRAMES";

            return true;
        });

        game.LoadGame(1U);

        const auto* pLb1 = game.FindLeaderboard(7U);
        Assert::IsNotNull(pLb1);
        Ensures(pLb1 != nullptr);
        Assert::AreEqual(std::string("LB1"), pLb1->Title());
        Assert::AreEqual(std::string("Desc1"), pLb1->Description());

        const auto* pLb2 = game.FindLeaderboard(8U);
        Assert::IsNotNull(pLb2);
        Ensures(pLb2 != nullptr);
        Assert::AreEqual(std::string("LB2"), pLb2->Title());
        Assert::AreEqual(std::string("Desc2"), pLb2->Description());
    }

    TEST_METHOD(TestLoadGameReplacesAchievements)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.Title = "Ach1";
            ach1.Description = "Desc1";
            ach1.Author = "Auth1";
            ach1.BadgeName = "12345";
            ach1.CategoryId = 3;
            ach1.Created = 1234567890;
            ach1.Updated = 1234599999;
            ach1.Definition = "1=1";
            ach1.Points = 5;

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 7;
            ach2.Title = "Ach2";
            ach2.Description = "Desc2";
            ach2.Author = "Auth2";
            ach2.BadgeName = "12345";
            ach2.CategoryId = 5;
            ach2.Created = 1234567890;
            ach2.Updated = 1234599999;
            ach2.Definition = "1=1";
            ach2.Points = 15;
            return true;
        });

        game.LoadGame(1U);

        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 9;
            ach1.Title = "Ach9";
            ach1.Description = "Desc9";
            ach1.Author = "Auth9";
            ach1.BadgeName = "12345";
            ach1.CategoryId = 3;
            ach1.Created = 1234567890;
            ach1.Updated = 1234599999;
            ach1.Definition = "1=1";
            ach1.Points = 9;

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 11;
            ach2.Title = "Ach11";
            ach2.Description = "Desc11";
            ach2.Author = "Auth11";
            ach2.BadgeName = "12345";
            ach2.CategoryId = 5;
            ach2.Created = 1234567890;
            ach2.Updated = 1234599999;
            ach2.Definition = "1=1";
            ach2.Points = 11;
            return true;
        });

        game.LoadGame(2U);

        Assert::IsNull(game.FindAchievement(5U));
        Assert::IsNull(game.FindAchievement(7U));

        const auto* pAch1 = game.FindAchievement(9U);
        Assert::IsNotNull(pAch1);
        Ensures(pAch1 != nullptr);
        Assert::AreEqual(std::string("Ach9"), pAch1->Title());
        Assert::AreEqual(std::string("Desc9"), pAch1->Description());
        Assert::AreEqual(std::string("Auth9"), pAch1->Author());
        Assert::AreEqual(std::string("12345"), pAch1->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Core, pAch1->GetCategory());
        Assert::AreEqual(9U, pAch1->Points());
        Assert::AreEqual(std::string("1=1"), pAch1->GetTrigger());

        const auto* pAch2 = game.FindAchievement(11U);
        Assert::IsNotNull(pAch2);
        Ensures(pAch2 != nullptr);
        Assert::AreEqual(std::string("Ach11"), pAch2->Title());
        Assert::AreEqual(std::string("Desc11"), pAch2->Description());
        Assert::AreEqual(std::string("Auth11"), pAch2->Author());
        Assert::AreEqual(std::string("12345"), pAch2->BadgeImageURI());
        Assert::AreEqual(Achievement::Category::Unofficial, pAch2->GetCategory());
        Assert::AreEqual(11U, pAch2->Points());
        Assert::AreEqual(std::string("1=1"), pAch2->GetTrigger());

        game.RemoveNonAchievementAssets();
        Assert::AreEqual({ 2U }, game.Assets().Count());
        Assert::IsFalse(game.Assets().IsUpdating());

        const auto* vmAch1 = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(0));
        Assert::IsNotNull(vmAch1);
        Ensures(vmAch1 != nullptr);
        Assert::AreEqual(9U, vmAch1->GetID());
        Assert::AreEqual(std::wstring(L"Ach9"), vmAch1->GetName());
        Assert::AreEqual(std::wstring(L"Desc9"), vmAch1->GetDescription());
        Assert::AreEqual(ra::data::models::AssetCategory::Core, vmAch1->GetCategory());
        Assert::AreEqual(9, vmAch1->GetPoints());
        Assert::AreEqual(std::wstring(L"12345"), vmAch1->GetBadge());
        Assert::AreEqual(std::string("1=1"), vmAch1->GetTrigger());
        Assert::IsFalse(vmAch1->IsModified());

        const auto* vmAch2 = dynamic_cast<ra::data::models::AchievementModel*>(game.Assets().GetItemAt(1));
        Assert::IsNotNull(vmAch2);
        Ensures(vmAch2 != nullptr);
        Assert::AreEqual(11U, vmAch2->GetID());
        Assert::AreEqual(std::wstring(L"Ach11"), vmAch2->GetName());
        Assert::AreEqual(std::wstring(L"Desc11"), vmAch2->GetDescription());
        Assert::AreEqual(ra::data::models::AssetCategory::Unofficial, vmAch2->GetCategory());
        Assert::AreEqual(11, vmAch2->GetPoints());
        Assert::AreEqual(std::wstring(L"12345"), vmAch2->GetBadge());
        Assert::AreEqual(std::string("1=1"), vmAch2->GetTrigger());
        Assert::IsFalse(vmAch2->IsModified());
    }

    TEST_METHOD(TestLoadGameZeroRemovesAchievements)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.Title = "Ach1";
            ach1.Description = "Desc1";
            ach1.Author = "Auth1";
            ach1.BadgeName = "12345";
            ach1.CategoryId = 3;
            ach1.Created = 1234567890;
            ach1.Updated = 1234599999;
            ach1.Definition = "1=1";
            ach1.Points = 5;

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 7;
            ach2.Title = "Ach2";
            ach2.Description = "Desc2";
            ach2.Author = "Auth2";
            ach2.BadgeName = "12345";
            ach2.CategoryId = 5;
            ach2.Created = 1234567890;
            ach2.Updated = 1234599999;
            ach2.Definition = "1=1";
            ach2.Points = 15;
            return true;
        });

        game.LoadGame(1U);

        game.RemoveNonAchievementAssets();
        Assert::AreEqual({ 2U }, game.Assets().Count());

        game.LoadGame(0U);

        Assert::IsNull(game.FindAchievement(5U));
        Assert::IsNull(game.FindAchievement(7U));

        Assert::AreEqual({ 0U }, game.Assets().Count());
        Assert::IsFalse(game.Assets().IsUpdating());
    }

    TEST_METHOD(TestLoadGameUserUnlocks)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.CategoryId = ra::etoi(Achievement::Category::Core);

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 7;
            ach2.CategoryId = ra::etoi(Achievement::Category::Core);
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request& request, ra::api::FetchUserUnlocks::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);
            Assert::IsFalse(request.Hardcore);

            response.UnlockedAchievements.insert(7U);
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchCodeNotes>([](const ra::api::FetchCodeNotes::Request&, ra::api::FetchCodeNotes::Response&)
        {
            return true;
        });

        game.LoadGame(1U);
        game.mockThreadPool.ExecuteNextTask(); // FetchUserUnlocks and FetchCodeNotes are async
        game.mockThreadPool.ExecuteNextTask();

        const auto* pAch1 = game.FindAchievement(5U);
        Assert::IsNotNull(pAch1);
        Ensures(pAch1 != nullptr);
        Assert::IsTrue(pAch1->Active());

        const auto* pAch2 = game.FindAchievement(7U);
        Assert::IsNotNull(pAch2);
        Ensures(pAch2 != nullptr);
        Assert::IsFalse(pAch2->Active());
    }

    TEST_METHOD(TestLoadGameUserUnlocksCompatibilityMode)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            auto& ach1 = response.Achievements.emplace_back();
            ach1.Id = 5;
            ach1.CategoryId = ra::etoi(Achievement::Category::Core);

            auto& ach2 = response.Achievements.emplace_back();
            ach2.Id = 7;
            ach2.CategoryId = ra::etoi(Achievement::Category::Core);
            return true;
        });

        game.mockServer.ExpectUncalled<ra::api::FetchUserUnlocks>();

        game.mockServer.HandleRequest<ra::api::FetchCodeNotes>([](const ra::api::FetchCodeNotes::Request&, ra::api::FetchCodeNotes::Response&)
        {
            return true;
        });

        game.LoadGame(1U, ra::data::context::GameContext::Mode::CompatibilityTest);
        game.mockThreadPool.ExecuteNextTask(); // FetchUserUnlocks and FetchCodeNotes are async
        game.mockThreadPool.ExecuteNextTask();

        const auto* pAch1 = game.FindAchievement(5U);
        Assert::IsNotNull(pAch1);
        Ensures(pAch1 != nullptr);
        Assert::IsTrue(pAch1->Active());

        const auto* pAch2 = game.FindAchievement(7U);
        Assert::IsNotNull(pAch2);
        Ensures(pAch2 != nullptr);
        Assert::IsTrue(pAch2->Active());
    }

    TEST_METHOD(TestLoadGamePausesRuntime)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchCodeNotes>([](const ra::api::FetchCodeNotes::Request&, ra::api::FetchCodeNotes::Response&)
        {
            return true;
        });

        game.LoadGame(1U);
        Assert::IsTrue(game.runtime.IsPaused());

        game.mockThreadPool.ExecuteNextTask(); // FetchUserUnlocks and FetchCodeNotes are async
        game.mockThreadPool.ExecuteNextTask();
        Assert::IsFalse(game.runtime.IsPaused());
    }

    TEST_METHOD(TestLoadGameWhileRuntimePaused)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchCodeNotes>([](const ra::api::FetchCodeNotes::Request&, ra::api::FetchCodeNotes::Response&)
        {
            return true;
        });

        game.runtime.SetPaused(true);

        game.LoadGame(1U);
        Assert::IsTrue(game.runtime.IsPaused());

        game.mockThreadPool.ExecuteNextTask(); // FetchUserUnlocks and FetchCodeNotes are async
        game.mockThreadPool.ExecuteNextTask();
        Assert::IsTrue(game.runtime.IsPaused());
    }

    TEST_METHOD(TestReloadRichPresenceScriptNoFile)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            response.RichPresence = "Display:\nHello, World";
            return true;
        });

        game.LoadGame(1U);

        /* load game will write the server RP to storage */
        game.ReloadRichPresenceScript();

        Assert::IsTrue(game.HasRichPresence());
        Assert::AreEqual(std::wstring(L"Hello, World"), game.GetRichPresenceDisplayString());
        Assert::IsFalse(game.IsRichPresenceFromFile());

        /* replace written server RP with empty file */
        game.mockStorage.DeleteStoredData(ra::services::StorageItemType::RichPresence, L"1");
        game.ReloadRichPresenceScript();

        Assert::IsFalse(game.HasRichPresence());
        Assert::AreEqual(std::wstring(L"No Rich Presence defined."), game.GetRichPresenceDisplayString());
        Assert::IsTrue(game.IsRichPresenceFromFile());
    }

    TEST_METHOD(TestReloadRichPresenceScriptWindowsLineEndings)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            response.RichPresence = "Display:\r\nHello, World\r\n";
            return true;
        });

        game.LoadGame(1U);

        /* load game will write the server RP to storage */
        game.ReloadRichPresenceScript();

        Assert::IsTrue(game.HasRichPresence());
        Assert::AreEqual(std::wstring(L"Hello, World"), game.GetRichPresenceDisplayString());
        Assert::IsFalse(game.IsRichPresenceFromFile());

        /* replace written server RP with a different file */
        game.mockStorage.MockStoredData(ra::services::StorageItemType::RichPresence, L"1", "Display:\r\nFrom File\r\n");
        game.ReloadRichPresenceScript();

        Assert::IsTrue(game.HasRichPresence());
        Assert::AreEqual(std::wstring(L"From File"), game.GetRichPresenceDisplayString());
        Assert::IsTrue(game.IsRichPresenceFromFile());
    }

    TEST_METHOD(TestReloadRichPresenceScript)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response& response)
        {
            response.RichPresence = "Display:\nHello, World";
            return true;
        });

        game.LoadGame(1U);

        /* load game will write the server RP to storage, so overwrite it now */
        game.mockStorage.MockStoredData(ra::services::StorageItemType::RichPresence, L"1", "Display:\nFrom File");
        game.ReloadRichPresenceScript();

        Assert::IsTrue(game.HasRichPresence());
        Assert::AreEqual(std::wstring(L"From File"), game.GetRichPresenceDisplayString());
        Assert::IsTrue(game.IsRichPresenceFromFile());
    }

    TEST_METHOD(TestAwardAchievementNonExistant)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        game.AwardAchievement(1U);

        Assert::IsFalse(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        Assert::IsFalse(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\acherror.wav"));
        Assert::IsNull(game.mockOverlayManager.GetMessage(1));

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievement)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.SetGameHash("hash");
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request& request, ra::api::AwardAchievement::Response& response)
        {
            Assert::AreEqual(1U, request.AchievementId);
            Assert::AreEqual(false, request.Hardcore);
            Assert::AreEqual(std::string("hash"), request.GameHash);

            response.NewPlayerScore = 125U;
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.MockAchievement();
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        game.mockThreadPool.ExecuteNextTask();
        Assert::AreEqual(125U, game.mockUser.GetScore());
    }

    TEST_METHOD(TestAwardAchievementHardcore)
    {
        GameContextHarness game;
        game.SetGameHash("hash");
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request& request, ra::api::AwardAchievement::Response& response)
        {
            Assert::AreEqual(1U, request.AchievementId);
            Assert::AreEqual(true, request.Hardcore);
            Assert::AreEqual(std::string("hash"), request.GameHash);

            response.NewPlayerScore = 125U;
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.MockAchievement();
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        game.mockThreadPool.ExecuteNextTask();
        Assert::AreEqual(125U, game.mockUser.GetScore());
    }

    TEST_METHOD(TestAwardAchievementNoPopup)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::None);
        game.SetGameHash("hash");
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request& request, ra::api::AwardAchievement::Response& response)
        {
            Assert::AreEqual(1U, request.AchievementId);
            Assert::AreEqual(false, request.Hardcore);
            Assert::AreEqual(std::string("hash"), request.GameHash);

            response.NewPlayerScore = 125U;
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.MockAchievement();
        game.AwardAchievement(1U);

        // sound should still be played
        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));

        // popup should not be displayed
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Assert::IsNull(pPopup);

        // API call should still occur
        game.mockThreadPool.ExecuteNextTask();
        Assert::AreEqual(125U, game.mockUser.GetScore());
    }

    TEST_METHOD(TestAwardAchievementLocal)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        game.MockAchievement().SetCategory(Achievement::Category::Local);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Local Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementUnofficial)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        game.MockAchievement().SetCategory(Achievement::Category::Unofficial);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Unofficial Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementModified)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        game.MockAchievement().SetModified(true);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\acherror.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Modified Achievement NOT Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementModifiedLocal)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        auto& pAch = game.MockAchievement();
        pAch.SetCategory(Achievement::Category::Local);
        pAch.SetModified(true);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\acherror.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Modified Local Achievement NOT Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementModifiedUnofficial)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        auto& pAch = game.MockAchievement();
        pAch.SetCategory(Achievement::Category::Unofficial);
        pAch.SetModified(true);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\acherror.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Modified Unofficial Achievement NOT Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementMemoryModified)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        game.MockAchievement();
        game.mockEmulator.MockMemoryModified(true);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\acherror.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement NOT Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Error: RAM tampered with"), pPopup->GetDetail());
        Assert::IsTrue(pPopup->IsDetailError());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementMemoryInsecureHardcore)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();

        game.MockAchievement();
        game.mockEmulator.MockMemoryInsecure(true);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\acherror.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement NOT Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Error: RAM insecure"), pPopup->GetDetail());
        Assert::IsTrue(pPopup->IsDetailError());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementMemoryInsecureNonHardcore)
    {
        GameContextHarness game;
        game.SetGameHash("hash");
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, false);
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request& request, ra::api::AwardAchievement::Response& response)
        {
            Assert::AreEqual(1U, request.AchievementId);
            Assert::AreEqual(false, request.Hardcore);
            Assert::AreEqual(std::string("hash"), request.GameHash);

            response.NewPlayerScore = 125U;
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.MockAchievement();
        game.mockEmulator.MockMemoryInsecure(true);
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementDuplicate)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request&, ra::api::AwardAchievement::Response& response)
        {
            response.ErrorMessage = "User already has this achievement awarded.";
            response.Result = ra::api::ApiResult::Error;
            return true;
        });

        game.MockAchievement();
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        game.mockThreadPool.ExecuteNextTask();

        // special error for "already unlocked" should not be reported
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());
    }

    TEST_METHOD(TestAwardAchievementDuplicateHardcore)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request&, ra::api::AwardAchievement::Response& response)
        {
            response.ErrorMessage = "User already has hardcore and regular achievements awarded.";
            response.Result = ra::api::ApiResult::Error;
            return true;
        });

        game.MockAchievement();
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        game.mockThreadPool.ExecuteNextTask();

        // special error for "already unlocked" should not be reported
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());
    }

    TEST_METHOD(TestAwardAchievementError)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request&, ra::api::AwardAchievement::Response& response)
        {
            response.ErrorMessage = "Achievement data cannot be found for 1";
            response.Result = ra::api::ApiResult::Error;
            return true;
        });

        game.MockAchievement();
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        game.mockThreadPool.ExecuteNextTask();

        // error message should be reported
        Assert::AreEqual(std::wstring(L"Achievement NOT Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Achievement data cannot be found for 1"), pPopup->GetDetail());
        Assert::IsTrue(pPopup->IsDetailError());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());
    }

    TEST_METHOD(TestAwardAchievementErrorAfterPopupDismissed)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request&, ra::api::AwardAchievement::Response& response)
        {
            response.ErrorMessage = "Achievement data cannot be found for 1";
            response.Result = ra::api::ApiResult::Error;
            return true;
        });

        game.MockAchievement();
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // if error occurs after original popup is gone, a new one should be created to display the error
        ra::services::ServiceLocator::ServiceOverride<ra::data::context::GameContext> contextOverride(&game, false);
        game.mockOverlayManager.ClearPopups();
        game.mockThreadPool.ExecuteNextTask();

        // error message should be reported
        pPopup = game.mockOverlayManager.GetMessage(2);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement NOT Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Achievement data cannot be found for 1"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());
    }

    TEST_METHOD(TestAwardAchievementCompatibilityMode)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockServer.ExpectUncalled<ra::api::AwardAchievement>();
        game.SetMode(ra::data::context::GameContext::Mode::CompatibilityTest);

        game.MockAchievement();
        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Test Achievement Unlocked"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"AchievementTitle (5)"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"AchievementDescription"), pPopup->GetDetail());
        Assert::AreEqual(std::string("12345"), pPopup->GetImage().Name());

        // AwardAchievement API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestAwardAchievementMasteryHardcore)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::Mastery, ra::ui::viewmodels::PopupLocation::TopMiddle);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.SetGameId(1U);
        game.SetGameHash("hash");
        game.SetGameTitle(L"GameName");
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request&, ra::api::AwardAchievement::Response& response)
        {
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response& response)
        {
            response.UnlockedAchievements.insert(1U);
            response.UnlockedAchievements.insert(2U);
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.mockConfiguration.SetUsername("Username");
        game.mockSessionTracker.MockSession(1, 123456789, std::chrono::seconds(78 * 60));

        game.MockAchievement();
        auto& pAch2 = game.MockAchievement();
        pAch2.SetID(2U);

        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());

        pPopup = game.mockOverlayManager.GetMessage(2);
        Assert::IsNull(pPopup);

        game.AwardAchievement(2U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        pPopup = game.mockOverlayManager.GetMessage(2);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());

        pPopup = game.mockOverlayManager.GetMessage(3);
        Assert::IsNull(pPopup);

        game.mockThreadPool.ExecuteNextTask(); // award achievement 1
        game.mockThreadPool.ExecuteNextTask(); // award achievement 2
        game.mockThreadPool.AdvanceTime(std::chrono::milliseconds(500)); // mastery unlock check is delayed
        game.mockThreadPool.ExecuteNextTask(); // check unlocks

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        pPopup = game.mockOverlayManager.GetMessage(3);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Mastered GameName"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"2 achievements, 10 points"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Username | Play time: 1h18m"), pPopup->GetDetail());
    }

    TEST_METHOD(TestAwardAchievementMasteryNonHardcore)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::Mastery, ra::ui::viewmodels::PopupLocation::TopMiddle);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, false);
        game.SetGameId(1U);
        game.SetGameHash("hash");
        game.SetGameTitle(L"GameName");
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request&, ra::api::AwardAchievement::Response& response)
        {
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response& response)
        {
            response.UnlockedAchievements.insert(1U);
            response.UnlockedAchievements.insert(2U);
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.mockConfiguration.SetUsername("Player");
        game.mockSessionTracker.MockSession(1, 123456789, std::chrono::seconds((102*60 + 3) * 60));

        game.MockAchievement();
        auto& pAch2 = game.MockAchievement();
        pAch2.SetID(2U);

        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());

        pPopup = game.mockOverlayManager.GetMessage(2);
        Assert::IsNull(pPopup);

        game.AwardAchievement(2U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        pPopup = game.mockOverlayManager.GetMessage(2);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());

        pPopup = game.mockOverlayManager.GetMessage(3);
        Assert::IsNull(pPopup);

        game.mockThreadPool.ExecuteNextTask(); // award achievement 1
        game.mockThreadPool.ExecuteNextTask(); // award achievement 2
        game.mockThreadPool.AdvanceTime(std::chrono::milliseconds(500)); // mastery unlock check is delayed
        game.mockThreadPool.ExecuteNextTask(); // check unlocks

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        pPopup = game.mockOverlayManager.GetMessage(3);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Completed GameName"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"2 achievements, 10 points"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Player | Play time: 102h03m"), pPopup->GetDetail());
    }

    TEST_METHOD(TestAwardAchievementMasteryIncomplete)
    {
        GameContextHarness game;
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::AchievementTriggered, ra::ui::viewmodels::PopupLocation::BottomLeft);
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::Mastery, ra::ui::viewmodels::PopupLocation::TopMiddle);
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.SetGameHash("hash");
        game.SetGameTitle(L"GameName");
        game.mockServer.HandleRequest<ra::api::AwardAchievement>([](const ra::api::AwardAchievement::Request&, ra::api::AwardAchievement::Response& response)
        {
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response& response)
        {
            response.UnlockedAchievements.insert(2U);
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.MockAchievement();
        auto& pAch2 = game.MockAchievement();
        pAch2.SetID(2U);

        game.AwardAchievement(1U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());

        pPopup = game.mockOverlayManager.GetMessage(2);
        Assert::IsNull(pPopup);

        game.AwardAchievement(2U);

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\unlock.wav"));
        pPopup = game.mockOverlayManager.GetMessage(2);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Achievement Unlocked"), pPopup->GetTitle());

        pPopup = game.mockOverlayManager.GetMessage(3);
        Assert::IsNull(pPopup);

        game.mockThreadPool.ExecuteNextTask(); // award achievement 1
        game.mockThreadPool.ExecuteNextTask(); // award achievement 2
        game.mockThreadPool.ExecuteNextTask(); // check unlocks

        // server indicates at least one achievement is not unlocked - reset by user?
        pPopup = game.mockOverlayManager.GetMessage(3);
        Assert::IsNull(pPopup);
    }

    TEST_METHOD(TestSubmitLeaderboardEntryNonExistant)
    {
        GameContextHarness game;
        game.mockServer.ExpectUncalled<ra::api::SubmitLeaderboardEntry>();

        game.SubmitLeaderboardEntry(1U, 1234U);

        Assert::IsFalse(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\info.wav"));
        Assert::IsNull(game.mockOverlayManager.GetMessage(1));

        // SubmitLeaderboardEntry API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();
    }

    TEST_METHOD(TestSubmitLeaderboardEntry)
    {
        GameContextHarness game;
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::LeaderboardScoreboard, ra::ui::viewmodels::PopupLocation::BottomRight);
        game.mockUser.Initialize("Player", "ApiToken");
        game.SetGameHash("hash");

        unsigned int nNewScore = 0U;
        game.mockServer.HandleRequest<ra::api::SubmitLeaderboardEntry>([&nNewScore]
            (const ra::api::SubmitLeaderboardEntry::Request& request, ra::api::SubmitLeaderboardEntry::Response& response)
        {
            Assert::AreEqual(1U, request.LeaderboardId);
            Assert::AreEqual(1234, request.Score);
            Assert::AreEqual(std::string("hash"), request.GameHash);
            nNewScore = request.Score;

            response.Result = ra::api::ApiResult::Success;
            response.TopEntries.push_back({ 1, "George", 1000U });
            response.TopEntries.push_back({ 2, "Player", 1234U });
            response.TopEntries.push_back({ 3, "Steve", 1500U });
            response.TopEntries.push_back({ 4, "Bill", 1700U });

            response.Score = 1234U;
            response.BestScore = 1234U;
            response.NewRank = 2;
            return true;
        });

        game.MockLeaderboard();
        game.SubmitLeaderboardEntry(1U, 1234U);

        game.mockThreadPool.ExecuteNextTask();
        Assert::AreEqual(1234U, nNewScore);

        const auto* vmScoreboard = game.mockOverlayManager.GetScoreboard(1U);
        Assert::IsNotNull(vmScoreboard);
        Ensures(vmScoreboard != nullptr);
        Assert::AreEqual(std::wstring(L"LeaderboardTitle"), vmScoreboard->GetHeaderText());
        Assert::AreEqual({ 4U }, vmScoreboard->Entries().Count());

        const auto* vmEntry1 = vmScoreboard->Entries().GetItemAt(0);
        Assert::IsNotNull(vmEntry1);
        Ensures(vmEntry1 != nullptr);
        Assert::AreEqual(1, vmEntry1->GetRank());
        Assert::AreEqual(std::wstring(L"George"), vmEntry1->GetUserName());
        Assert::AreEqual(std::wstring(L"1000"), vmEntry1->GetScore());
        Assert::IsFalse(vmEntry1->IsHighlighted());

        const auto* vmEntry2 = vmScoreboard->Entries().GetItemAt(1);
        Assert::IsNotNull(vmEntry2);
        Ensures(vmEntry2 != nullptr);
        Assert::AreEqual(2, vmEntry2->GetRank());
        Assert::AreEqual(std::wstring(L"Player"), vmEntry2->GetUserName());
        Assert::AreEqual(std::wstring(L"1234"), vmEntry2->GetScore());
        Assert::IsTrue(vmEntry2->IsHighlighted());

        const auto* vmEntry3 = vmScoreboard->Entries().GetItemAt(2);
        Assert::IsNotNull(vmEntry3);
        Ensures(vmEntry3 != nullptr);
        Assert::AreEqual(3, vmEntry3->GetRank());
        Assert::AreEqual(std::wstring(L"Steve"), vmEntry3->GetUserName());
        Assert::AreEqual(std::wstring(L"1500"), vmEntry3->GetScore());
        Assert::IsFalse(vmEntry3->IsHighlighted());

        const auto* vmEntry4 = vmScoreboard->Entries().GetItemAt(3);
        Assert::IsNotNull(vmEntry4);
        Ensures(vmEntry4 != nullptr);
        Assert::AreEqual(4, vmEntry4->GetRank());
        Assert::AreEqual(std::wstring(L"Bill"), vmEntry4->GetUserName());
        Assert::AreEqual(std::wstring(L"1700"), vmEntry4->GetScore());
        Assert::IsFalse(vmEntry4->IsHighlighted());
    }

    TEST_METHOD(TestSubmitLeaderboardEntryScoreboardDisabled)
    {
        GameContextHarness game;
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::LeaderboardScoreboard, ra::ui::viewmodels::PopupLocation::None);
        game.mockUser.Initialize("Player", "ApiToken");
        game.SetGameHash("hash");

        unsigned int nNewScore = 0U;
        game.mockServer.HandleRequest<ra::api::SubmitLeaderboardEntry>([&nNewScore]
        (const ra::api::SubmitLeaderboardEntry::Request& request, ra::api::SubmitLeaderboardEntry::Response& response)
        {
            nNewScore = request.Score;

            response.Result = ra::api::ApiResult::Success;
            response.TopEntries.push_back({ 1, "George", 1000U });
            response.TopEntries.push_back({ 2, "Player", 1234U });
            response.TopEntries.push_back({ 3, "Steve", 1500U });
            response.TopEntries.push_back({ 4, "Bill", 1700U });
            return true;
        });

        game.MockLeaderboard();
        game.SubmitLeaderboardEntry(1U, 1234U);

        game.mockThreadPool.ExecuteNextTask();
        Assert::AreEqual(1234U, nNewScore);

        const auto* vmScoreboard = game.mockOverlayManager.GetScoreboard(1U);
        Assert::IsNull(vmScoreboard);
    }

    TEST_METHOD(TestSubmitLeaderboardEntryNonHardcore)
    {
        GameContextHarness game;
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, false);
        game.SetGameHash("hash");

        game.mockServer.ExpectUncalled<ra::api::SubmitLeaderboardEntry>();

        game.MockLeaderboard();
        game.SubmitLeaderboardEntry(1U, 1234U);

        // SubmitLeaderboardEntry API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\info.wav"));

        // error message should be reported
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Leaderboard NOT Submitted"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"LeaderboardTitle"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Submission requires Hardcore mode"), pPopup->GetDetail());
        Assert::IsTrue(pPopup->IsDetailError());
    }

    TEST_METHOD(TestSubmitLeaderboardEntryCompatibilityMode)
    {
        GameContextHarness game;
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.SetMode(ra::data::context::GameContext::Mode::CompatibilityTest);
        game.SetGameHash("hash");

        game.mockServer.ExpectUncalled<ra::api::SubmitLeaderboardEntry>();

        game.MockLeaderboard();
        game.SubmitLeaderboardEntry(1U, 1234U);

        // SubmitLeaderboardEntry API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\info.wav"));

        // error message should be reported
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Leaderboard NOT Submitted"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"LeaderboardTitle"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Leaderboards are not submitted in test mode."), pPopup->GetDetail());
        Assert::IsFalse(pPopup->IsDetailError());
    }

    TEST_METHOD(TestSubmitLeaderboardEntryLowRank)
    {
        GameContextHarness game;
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::LeaderboardScoreboard, ra::ui::viewmodels::PopupLocation::BottomRight);
        game.mockUser.Initialize("Player", "ApiToken");
        game.SetGameHash("hash");

        game.mockServer.HandleRequest<ra::api::SubmitLeaderboardEntry>([]
            (const ra::api::SubmitLeaderboardEntry::Request&, ra::api::SubmitLeaderboardEntry::Response& response)
        {
            response.Result = ra::api::ApiResult::Success;
            response.TopEntries.push_back({ 1, "George", 1000U });
            response.TopEntries.push_back({ 2, "Philip", 1100U });
            response.TopEntries.push_back({ 3, "Steve", 1200U });
            response.TopEntries.push_back({ 4, "Bill", 1300U });
            response.TopEntries.push_back({ 5, "Roger", 1400U });
            response.TopEntries.push_back({ 6, "Andy", 1500U });
            response.TopEntries.push_back({ 7, "Jason", 1600U });
            response.TopEntries.push_back({ 8, "Jeff", 1700U });

            response.Score = 2000U;
            response.BestScore = 1900U;
            response.NewRank = 11;
            return true;
        });

        game.MockLeaderboard();
        game.SubmitLeaderboardEntry(1U, 2000U);

        game.mockThreadPool.ExecuteNextTask();

        const auto* vmScoreboard = game.mockOverlayManager.GetScoreboard(1U);
        Assert::IsNotNull(vmScoreboard);
        Ensures(vmScoreboard != nullptr);
        Assert::AreEqual(std::wstring(L"LeaderboardTitle"), vmScoreboard->GetHeaderText());
        Assert::AreEqual({ 7U }, vmScoreboard->Entries().Count());

        const auto* vmEntry1 = vmScoreboard->Entries().GetItemAt(0);
        Assert::IsNotNull(vmEntry1);
        Ensures(vmEntry1 != nullptr);
        Assert::AreEqual(1, vmEntry1->GetRank());
        Assert::AreEqual(std::wstring(L"George"), vmEntry1->GetUserName());
        Assert::AreEqual(std::wstring(L"1000"), vmEntry1->GetScore());
        Assert::IsFalse(vmEntry1->IsHighlighted());

        const auto* vmEntry6 = vmScoreboard->Entries().GetItemAt(5);
        Assert::IsNotNull(vmEntry6);
        Ensures(vmEntry6 != nullptr);
        Assert::AreEqual(6, vmEntry6->GetRank());
        Assert::AreEqual(std::wstring(L"Andy"), vmEntry6->GetUserName());
        Assert::AreEqual(std::wstring(L"1500"), vmEntry6->GetScore());
        Assert::IsFalse(vmEntry6->IsHighlighted());

        const auto* vmEntry7 = vmScoreboard->Entries().GetItemAt(6);
        Assert::IsNotNull(vmEntry7);
        Ensures(vmEntry7 != nullptr);
        Assert::AreEqual(11, vmEntry7->GetRank());
        Assert::AreEqual(std::wstring(L"Player"), vmEntry7->GetUserName());
        Assert::AreEqual(std::wstring(L"(2000) 1900"), vmEntry7->GetScore());
        Assert::IsTrue(vmEntry7->IsHighlighted());
    }

    TEST_METHOD(TestSubmitLeaderboardEntryScoreNotImproved)
    {
        GameContextHarness game;
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);
        game.mockConfiguration.SetPopupLocation(ra::ui::viewmodels::Popup::LeaderboardScoreboard, ra::ui::viewmodels::PopupLocation::BottomRight);
        game.mockUser.Initialize("Player", "ApiToken");
        game.SetGameHash("hash");

        unsigned int nNewScore = 0U;
        game.mockServer.HandleRequest<ra::api::SubmitLeaderboardEntry>([&nNewScore]
        (const ra::api::SubmitLeaderboardEntry::Request & request, ra::api::SubmitLeaderboardEntry::Response & response)
        {
            Assert::AreEqual(1U, request.LeaderboardId);
            Assert::AreEqual(1234, request.Score);
            Assert::AreEqual(std::string("hash"), request.GameHash);
            nNewScore = request.Score;

            response.Result = ra::api::ApiResult::Success;
            response.TopEntries.push_back({ 1, "George", 1000U });
            response.TopEntries.push_back({ 2, "Player", 1200U });
            response.TopEntries.push_back({ 3, "Steve", 1500U });
            response.TopEntries.push_back({ 4, "Bill", 1700U });

            response.Score = 1234U;
            response.BestScore = 1200U;
            response.NewRank = 2;
            return true;
        });

        game.MockLeaderboard();
        game.SubmitLeaderboardEntry(1U, 1234U);

        game.mockThreadPool.ExecuteNextTask();
        Assert::AreEqual(1234U, nNewScore);

        const auto* vmScoreboard = game.mockOverlayManager.GetScoreboard(1U);
        Assert::IsNotNull(vmScoreboard);
        Ensures(vmScoreboard != nullptr);
        Assert::AreEqual(std::wstring(L"LeaderboardTitle"), vmScoreboard->GetHeaderText());
        Assert::AreEqual({ 4U }, vmScoreboard->Entries().Count());

        const auto* vmEntry2 = vmScoreboard->Entries().GetItemAt(1);
        Assert::IsNotNull(vmEntry2);
        Ensures(vmEntry2 != nullptr);
        Assert::AreEqual(2, vmEntry2->GetRank());
        Assert::AreEqual(std::wstring(L"Player"), vmEntry2->GetUserName());
        Assert::AreEqual(std::wstring(L"(1234) 1200"), vmEntry2->GetScore());
        Assert::IsTrue(vmEntry2->IsHighlighted());
    }

    TEST_METHOD(TestSubmitLeaderboardEntryMemoryModified)
    {
        GameContextHarness game;
        game.SetGameHash("hash");

        game.mockServer.ExpectUncalled<ra::api::SubmitLeaderboardEntry>();

        game.MockLeaderboard();
        game.mockEmulator.MockMemoryModified(true);
        game.SubmitLeaderboardEntry(1U, 1234U);

        // SubmitLeaderboardEntry API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\info.wav"));

        // error message should be reported
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Leaderboard NOT Submitted"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"LeaderboardTitle"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Error: RAM tampered with"), pPopup->GetDetail());
        Assert::IsTrue(pPopup->IsDetailError());
    }

    TEST_METHOD(TestSubmitLeaderboardEntryMemoryInsecure)
    {
        GameContextHarness game;
        game.SetGameHash("hash");
        game.mockConfiguration.SetFeatureEnabled(ra::services::Feature::Hardcore, true);

        game.mockServer.ExpectUncalled<ra::api::SubmitLeaderboardEntry>();

        game.MockLeaderboard();
        game.mockEmulator.MockMemoryInsecure(true);
        game.SubmitLeaderboardEntry(1U, 1234U);

        // SubmitLeaderboardEntry API call is async, try to execute it - expect no tasks queued
        game.mockThreadPool.ExecuteNextTask();

        Assert::IsTrue(game.mockAudioSystem.WasAudioFilePlayed(L"Overlay\\info.wav"));

        // error message should be reported
        const auto* pPopup = game.mockOverlayManager.GetMessage(1);
        Expects(pPopup != nullptr);
        Assert::IsNotNull(pPopup);
        Assert::AreEqual(std::wstring(L"Leaderboard NOT Submitted"), pPopup->GetTitle());
        Assert::AreEqual(std::wstring(L"LeaderboardTitle"), pPopup->GetDescription());
        Assert::AreEqual(std::wstring(L"Error: RAM insecure"), pPopup->GetDetail());
        Assert::IsTrue(pPopup->IsDetailError());
    }

    TEST_METHOD(TestLoadCodeNotes)
    {
        GameContextNotifyTarget notifyTarget;

        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchCodeNotes>([](const ra::api::FetchCodeNotes::Request& request, ra::api::FetchCodeNotes::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);

            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 1234, L"Note1", "Author" });
            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 2345, L"Note2", "Author" });
            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 3456, L"Note3", "Author" });
            return true;
        });

        game.AddNotifyTarget(notifyTarget);

        game.LoadGame(1U);
        game.mockThreadPool.ExecuteNextTask(); // FetchUserUnlocks and FetchCodeNotes are async
        game.mockThreadPool.ExecuteNextTask();

        const auto* pNote1 = game.FindCodeNote(1234U);
        Assert::IsNotNull(pNote1);
        Ensures(pNote1 != nullptr);
        Assert::AreEqual(std::wstring(L"Note1"), *pNote1);
        const auto* pNote1b = notifyTarget.GetNewCodeNote(1234U);
        Assert::IsNotNull(pNote1b);
        Ensures(pNote1b != nullptr);
        Assert::AreEqual(std::wstring(L"Note1"), *pNote1b);

        const auto* pNote2 = game.FindCodeNote(2345U);
        Assert::IsNotNull(pNote2);
        Ensures(pNote2 != nullptr);
        Assert::AreEqual(std::wstring(L"Note2"), *pNote2);
        const auto* pNote2b = notifyTarget.GetNewCodeNote(2345U);
        Assert::IsNotNull(pNote2b);
        Ensures(pNote2b != nullptr);
        Assert::AreEqual(std::wstring(L"Note2"), *pNote2b);

        const auto* pNote3 = game.FindCodeNote(3456U);
        Assert::IsNotNull(pNote3);
        Ensures(pNote3 != nullptr);
        Assert::AreEqual(std::wstring(L"Note3"), *pNote3);
        const auto* pNote3b = notifyTarget.GetNewCodeNote(3456U);
        Assert::IsNotNull(pNote3b);
        Ensures(pNote3b != nullptr);
        Assert::AreEqual(std::wstring(L"Note3"), *pNote3b);

        const auto* pNote4 = game.FindCodeNote(4567U);
        Assert::IsNull(pNote4);
        const auto* pNote4b = notifyTarget.GetNewCodeNote(4567U);
        Assert::IsNull(pNote4b);

        game.LoadGame(0U);
        const auto* pNote5 = game.FindCodeNote(1234U);
        Assert::IsNull(pNote5);
        const auto* pNote5b = notifyTarget.GetNewCodeNote(1234U);
        Assert::IsNull(pNote5b);
    }

    void TestCodeNoteSize(const std::wstring& sNote, unsigned int nExpectedSize)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchCodeNotes>([&sNote](const ra::api::FetchCodeNotes::Request& request, ra::api::FetchCodeNotes::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);

            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 1234, sNote, "Author" });
            return true;
        });

        game.LoadGame(1U);
        game.mockThreadPool.ExecuteNextTask(); // FetchUserUnlocks and FetchCodeNotes are async
        game.mockThreadPool.ExecuteNextTask();

        Assert::AreEqual(nExpectedSize, game.GetCodeNoteSize(1234U));
    }

    TEST_METHOD(TestLoadCodeNotesSized)
    {
        TestCodeNoteSize(L"Test", 1U);
        TestCodeNoteSize(L"[16-bit] Test", 2U);
        TestCodeNoteSize(L"[16 bit] Test", 2U);
        TestCodeNoteSize(L"[16 Bit] Test", 2U);
        TestCodeNoteSize(L"[32-bit] Test", 4U);
        TestCodeNoteSize(L"[32 bit] Test", 4U);
        TestCodeNoteSize(L"[32bit] Test", 4U);
        TestCodeNoteSize(L"Test [16-bit]", 2U);
        TestCodeNoteSize(L"Test (16-bit)", 2U);
        TestCodeNoteSize(L"[2 Byte] Test", 2U);
        TestCodeNoteSize(L"[4 Byte] Test", 4U);
        TestCodeNoteSize(L"[4 Byte - Float] Test", 4U);
        TestCodeNoteSize(L"[8 Byte] Test", 8U);
        TestCodeNoteSize(L"[100 Bytes] Test", 100U);
        TestCodeNoteSize(L"[2 byte] Test", 2U);
        TestCodeNoteSize(L"[2-byte] Test", 2U);
        TestCodeNoteSize(L"Test (6 bytes)", 6U);
        TestCodeNoteSize(L"[2byte] Test", 2U);
        TestCodeNoteSize(L"4=bitten", 1U);
        TestCodeNoteSize(L"bit by bit", 1U);
    }

    TEST_METHOD(TestFindCodeNoteSized)
    {
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::FetchGameData>([](const ra::api::FetchGameData::Request&, ra::api::FetchGameData::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchUserUnlocks>([](const ra::api::FetchUserUnlocks::Request&, ra::api::FetchUserUnlocks::Response&)
        {
            return true;
        });

        game.mockServer.HandleRequest<ra::api::FetchCodeNotes>([](const ra::api::FetchCodeNotes::Request& request, ra::api::FetchCodeNotes::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);

            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 1000, L"[32-bit] Location", "Author" });
            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 1100, L"Level", "Author" });
            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 1110, L"[16-bit] Strength", "Author" });
            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 1120, L"[8 byte] Exp", "Author" });
            response.Notes.emplace_back(ra::api::FetchCodeNotes::Response::CodeNote{ 1200, L"[20 bytes] Items", "Author" });
            return true;
        });

        game.LoadGame(1U);
        game.mockThreadPool.ExecuteNextTask(); // FetchUserUnlocks and FetchCodeNotes are async
        game.mockThreadPool.ExecuteNextTask();

        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(100, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(999, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [1/4]"), game.FindCodeNote(1000, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [2/4]"), game.FindCodeNote(1001, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [3/4]"), game.FindCodeNote(1002, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [4/4]"), game.FindCodeNote(1003, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(1004, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"Level"), game.FindCodeNote(1100, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength [1/2]"), game.FindCodeNote(1110, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength [2/2]"), game.FindCodeNote(1111, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [1/8]"), game.FindCodeNote(1120, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [2/8]"), game.FindCodeNote(1121, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [3/8]"), game.FindCodeNote(1122, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [4/8]"), game.FindCodeNote(1123, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [5/8]"), game.FindCodeNote(1124, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [6/8]"), game.FindCodeNote(1125, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [7/8]"), game.FindCodeNote(1126, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[8 byte] Exp [8/8]"), game.FindCodeNote(1127, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(1128, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[20 bytes] Items [1/20]"), game.FindCodeNote(1200, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[20 bytes] Items [10/20]"), game.FindCodeNote(1209, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L"[20 bytes] Items [20/20]"), game.FindCodeNote(1219, MemSize::EightBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(1300, MemSize::EightBit));

        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(100, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(998, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(999, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(1000, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(1001, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(1002, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(1003, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(1004, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"Level [partial]"), game.FindCodeNote(1099, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"Level [partial]"), game.FindCodeNote(1100, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength [partial]"), game.FindCodeNote(1109, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength"), game.FindCodeNote(1110, MemSize::SixteenBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength [partial]"), game.FindCodeNote(1111, MemSize::SixteenBit));

        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(100, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(996, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(997, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location"), game.FindCodeNote(1000, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(1001, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(1002, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[32-bit] Location [partial]"), game.FindCodeNote(1003, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(1004, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"Level [partial]"), game.FindCodeNote(1097, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"Level [partial]"), game.FindCodeNote(1100, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength [partial]"), game.FindCodeNote(1107, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength [partial]"), game.FindCodeNote(1110, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L"[16-bit] Strength [partial]"), game.FindCodeNote(1111, MemSize::ThirtyTwoBit));
        Assert::AreEqual(std::wstring(L""), game.FindCodeNote(1112, MemSize::ThirtyTwoBit));
    }

    TEST_METHOD(TestSetCodeNote)
    {
        GameContextNotifyTarget notifyTarget;
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::UpdateCodeNote>([](const ra::api::UpdateCodeNote::Request& request, ra::api::UpdateCodeNote::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);
            Assert::AreEqual(1234U, request.Address);
            Assert::AreEqual(std::wstring(L"Note1"), request.Note);

            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.AddNotifyTarget(notifyTarget);

        game.SetGameId(1U);
        Assert::IsTrue(game.SetCodeNote(1234, L"Note1"));

        const auto* pNote1 = game.FindCodeNote(1234U);
        Assert::IsNotNull(pNote1);
        Ensures(pNote1 != nullptr);
        Assert::AreEqual(std::wstring(L"Note1"), *pNote1);
        const auto* pNote1b = notifyTarget.GetNewCodeNote(1234U);
        Assert::IsNotNull(pNote1b);
        Ensures(pNote1b != nullptr);
        Assert::AreEqual(std::wstring(L"Note1"), *pNote1b);
    }

    TEST_METHOD(TestSetCodeNoteGameNotLoaded)
    {
        GameContextHarness game;
        game.mockServer.ExpectUncalled<ra::api::UpdateCodeNote>();

        game.SetGameId(0U);
        Assert::IsFalse(game.SetCodeNote(1234, L"Note1"));

        const auto* pNote1 = game.FindCodeNote(1234U);
        Assert::IsNull(pNote1);
    }

    TEST_METHOD(TestUpdateCodeNote)
    {
        GameContextNotifyTarget notifyTarget;

        int nCalls = 0;
        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::UpdateCodeNote>([&nCalls](const ra::api::UpdateCodeNote::Request&, ra::api::UpdateCodeNote::Response& response)
        {
            ++nCalls;
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.AddNotifyTarget(notifyTarget);

        game.SetGameId(1U);
        Assert::IsTrue(game.SetCodeNote(1234, L"Note1"));

        const auto* pNote1 = game.FindCodeNote(1234U);
        Assert::IsNotNull(pNote1);
        Ensures(pNote1 != nullptr);
        Assert::AreEqual(std::wstring(L"Note1"), *pNote1);

        Assert::IsTrue(game.SetCodeNote(1234, L"Note1b"));
        const auto* pNote1b = game.FindCodeNote(1234U);
        Assert::IsNotNull(pNote1b);
        Ensures(pNote1b != nullptr);
        Assert::AreEqual(std::wstring(L"Note1b"), *pNote1b);

        const auto* pNote1x = notifyTarget.GetNewCodeNote(1234U);
        Assert::IsNotNull(pNote1x);
        Ensures(pNote1x != nullptr);
        Assert::AreEqual(std::wstring(L"Note1b"), *pNote1x);

        Assert::AreEqual(2, nCalls);
    }

    TEST_METHOD(TestDeleteCodeNote)
    {
        GameContextNotifyTarget notifyTarget;

        GameContextHarness game;
        game.mockServer.HandleRequest<ra::api::UpdateCodeNote>([](const ra::api::UpdateCodeNote::Request&, ra::api::UpdateCodeNote::Response& response)
        {
            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.mockServer.HandleRequest<ra::api::DeleteCodeNote>([](const ra::api::DeleteCodeNote::Request& request, ra::api::DeleteCodeNote::Response& response)
        {
            Assert::AreEqual(1U, request.GameId);
            Assert::AreEqual(1234U, request.Address);

            response.Result = ra::api::ApiResult::Success;
            return true;
        });

        game.AddNotifyTarget(notifyTarget);

        game.SetGameId(1U);
        Assert::IsTrue(game.SetCodeNote(1234, L"Note1"));

        const auto* pNote1 = game.FindCodeNote(1234U);
        Assert::IsNotNull(pNote1);
        Ensures(pNote1 != nullptr);
        Assert::AreEqual(std::wstring(L"Note1"), *pNote1);

        Assert::IsTrue(game.DeleteCodeNote(1234));
        const auto* pNote1b = game.FindCodeNote(1234U);
        Assert::IsNull(pNote1b);

        const auto* pNote1x = notifyTarget.GetNewCodeNote(1234U);
        Assert::IsNotNull(pNote1x);
        Ensures(pNote1x != nullptr);
        Assert::AreEqual(std::wstring(), *pNote1x);
    }

    TEST_METHOD(TestDeleteCodeNoteNonExistant)
    {
        GameContextHarness game;
        game.mockServer.ExpectUncalled<ra::api::DeleteCodeNote>();

        game.SetGameId(1U);

        Assert::IsTrue(game.DeleteCodeNote(1234));
        const auto* pNote1 = game.FindCodeNote(1234U);
        Assert::IsNull(pNote1);
    }
};

} // namespace tests
} // namespace context
} // namespace data
} // namespace ra
