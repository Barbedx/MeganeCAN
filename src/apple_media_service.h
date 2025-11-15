#pragma once
#include <string>
#include <functional>
 

class BLEClient;

namespace AppleMediaService
{
    // Remote commands supported by AMS
    enum class RemoteCommandID : uint8_t
    {
        Play = 0,
        Pause = 1,
        TogglePlayPause = 2,
        NextTrack = 3,
        PreviousTrack = 4,
        VolumeUp = 5,
        VolumeDown = 6,
        AdvanceRepeatMode = 7,
        AdvanceShuffleMode = 8,
        SkipForward = 9,
        SkipBackward = 10,
        LikeTrack = 11,
        DislikeTrack = 12,
        BookmarkTrack = 13,
    };

    struct MediaInformation
    {
        enum class PlaybackState : uint8_t
        {
            Paused = 0,
            Playing = 1,
            Rewinding = 2,
            FastForwarding = 3
        };

        enum class ShuffleMode : uint8_t
        {
            Off = 0,
            One = 1,
            All = 2
        };

        enum class RepeatMode : uint8_t
        {
            Off = 0,
            One = 1,
            All = 2
        };
        // PlayerAttributeID
        std::string mPlayerName;
        PlaybackState mPlaybackState;
        float mPlaybackRate;
        float mElapsedTime;
        float mVolume;
        // QueueAttributeID
        int mQueueIndex;
        int mQueueCount;
        ShuffleMode mShuffleMode;
        RepeatMode mRepeatMode;
        // TrackAttributeID
        std::string mArtist;
        std::string mAlbum;
        std::string mTitle;
        float mDuration;
        
        // нове поле: коли ми останній раз отримали playbackInfo від телефону
        uint32_t mLastPlaybackInfoMs = 0;

        void dump() const;

    };

    using NotificationCb = std::function<void(const MediaInformation &)>;

    enum class NotificationLevel
    {
        All,           // get notified anytime anything changes. Probably too noisey.
        TrackTitleOnly // only get notified when the track title is set.
    };
    // overwrites existing notification if set.
    void RegisterForNotifications(const NotificationCb &callback, NotificationLevel level);
    const MediaInformation &GetMediaInformation();
    bool setRemoteCommandValue(uint8_t commandID);
    // Low-level sender (wraps your existing setRemoteCommandValue)
    inline bool SendRemoteCommand(RemoteCommandID cmd)
    {
        return setRemoteCommandValue(static_cast<uint8_t>(cmd));
    }
    // Convenience helpers
    inline bool Play() { return SendRemoteCommand(RemoteCommandID::Play); }
    inline bool Pause() { return SendRemoteCommand(RemoteCommandID::Pause); }
    inline bool Toggle() { return SendRemoteCommand(RemoteCommandID::TogglePlayPause); }
    inline bool NextTrack() { return SendRemoteCommand(RemoteCommandID::NextTrack); }
    inline bool PrevTrack() { return SendRemoteCommand(RemoteCommandID::PreviousTrack); }
    inline bool VolumeUp() { return SendRemoteCommand(RemoteCommandID::VolumeUp); }
    inline bool VolumeDown() { return SendRemoteCommand(RemoteCommandID::VolumeDown); }
    inline bool SkipForward() { return SendRemoteCommand(RemoteCommandID::SkipForward); }
    inline bool SkipBack() { return SendRemoteCommand(RemoteCommandID::SkipBackward); }
    inline bool AdvanceRepeatMode() { return SendRemoteCommand(RemoteCommandID::AdvanceRepeatMode); }
    inline bool AdvanceShuffleMode() { return SendRemoteCommand(RemoteCommandID::AdvanceShuffleMode); }
    inline bool LikeTrack() { return SendRemoteCommand(RemoteCommandID::LikeTrack); }
    inline bool DislikeTrack() { return SendRemoteCommand(RemoteCommandID::DislikeTrack); }
    inline bool BookmarkTrack() { return SendRemoteCommand(RemoteCommandID::BookmarkTrack); }

    bool StartMediaService(BLEClient *client);
}