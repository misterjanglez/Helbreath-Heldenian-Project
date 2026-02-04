// EntityMotion.h: Smooth entity movement interpolation system
//
// Separates animation frames (sprite selection) from movement position (interpolation)
// Provides smooth per-pixel movement at any framerate
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

//=============================================================================
// Movement Timing Constants
//=============================================================================
namespace MovementTiming {
    // Duration to cross one tile (milliseconds)
    // Must match animation frame timing: (maxFrame + 1) * frameTime
    // Movement duration = (maxFrame + 1) * frameTime
    constexpr uint32_t WALK_DURATION_MS = 592;         // 8 frames (0-7) * 74ms
    constexpr uint32_t RUN_DURATION_MS = 312;          // 8 frames (0-7) * 39ms
    constexpr uint32_t DAMAGE_MOVE_DURATION_MS = 200;  // 4 frames (0-3) * 50ms
    constexpr uint32_t ATTACK_MOVE_DURATION_MS = 1014; // 13 frames (0-12) * 78ms

    // Tile size in pixels (matches original offset range of ~30)
    constexpr int16_t TILE_SIZE = 32;
}

//=============================================================================
// EntityMotion - Interpolation state for a single entity
//=============================================================================
struct EntityMotion {
    //-------------------------------------------------------------------------
    // State
    //-------------------------------------------------------------------------
    bool bIsMoving = false;
    int8_t cDirection = 0;          // 1-8 cardinal/ordinal directions

    //-------------------------------------------------------------------------
    // Interpolation
    //-------------------------------------------------------------------------
    float fProgress = 0.0f;         // 0.0 (start) to 1.0 (complete)

    //-------------------------------------------------------------------------
    // Timing
    //-------------------------------------------------------------------------
    uint32_t dwStartTime = 0;       // When movement started (ms)
    uint32_t dwDuration = 0;        // Total movement duration (ms)

    //-------------------------------------------------------------------------
    // Pixel Offsets (from current tile center)
    // Start: offset where entity came FROM
    // Target: always (0, 0) - the tile center
    // Using floats to support seamless tile transitions with non-integer offsets
    //-------------------------------------------------------------------------
    float fStartOffsetX = 0.0f;
    float fStartOffsetY = 0.0f;

    //-------------------------------------------------------------------------
    // Calculated Output (updated each frame) - floats for smooth interpolation
    //-------------------------------------------------------------------------
    float fCurrentOffsetX = 0.0f;
    float fCurrentOffsetY = 0.0f;

    //-------------------------------------------------------------------------
    // Pending next movement (2-slot queue for seamless chaining)
    //-------------------------------------------------------------------------
    bool bHasPending = false;
    int8_t cPendingDirection = 0;
    uint32_t dwPendingDuration = 0;

    //-------------------------------------------------------------------------
    // Methods
    //-------------------------------------------------------------------------

    // Start movement in given direction
    // direction: 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW
    // currentTime: current game time in milliseconds
    // duration: time to complete movement in milliseconds
    void StartMove(int8_t direction, uint32_t currentTime, uint32_t duration);

    // Start movement with a custom initial offset (for seamless tile transitions)
    // This allows continuing from where a previous motion left off
    // offsetX, offsetY: The starting offset (can be outside normal [-32, 0] range for seamless transitions)
    void StartMoveWithOffset(int8_t direction, uint32_t currentTime, uint32_t duration, float offsetX, float offsetY);

    // Queue a follow-up move (called when move arrives while still interpolating)
    void QueueMove(int8_t direction, uint32_t duration);

    // Update interpolation - call every frame
    // currentTime: current game time in milliseconds
    void Update(uint32_t currentTime);

    // Stop movement smoothly (at current position)
    void Stop();

    // Hard snap back to tile center (collision/bump)
    // Preserves the classic jarring bump feel
    void Bump();

    // Reset to default state
    void Reset();

    //-------------------------------------------------------------------------
    // Queries
    //-------------------------------------------------------------------------
    bool IsComplete() const { return !bIsMoving && fProgress >= 1.0f; }
    bool IsMoving() const { return bIsMoving; }
    bool HasPending() const { return bHasPending; }

    //-------------------------------------------------------------------------
    // Static Helpers
    //-------------------------------------------------------------------------

    // Get starting pixel offset for a movement direction
    // Entity moving in direction D came FROM the opposite direction
    // Returns the pixel offset from tile center where movement starts
    static void GetDirectionStartOffset(int8_t direction, int16_t& outX, int16_t& outY);

    // Get movement duration for an action type
    static uint32_t GetDurationForAction(int action, bool hasHaste = false, bool isFrozen = false);
};
