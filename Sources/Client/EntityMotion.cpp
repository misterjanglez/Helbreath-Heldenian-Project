// EntityMotion.cpp: Smooth entity movement interpolation implementation
//
//////////////////////////////////////////////////////////////////////

#include "EntityMotion.h"
#include "ActionID.h"
#include <cmath>
#include <algorithm>

//=============================================================================
// StartMove - Begin movement interpolation in a direction
//=============================================================================
void EntityMotion::StartMove(int8_t direction, uint32_t currentTime, uint32_t duration)
{
    cDirection = direction;
    dwStartTime = currentTime;
    dwDuration = duration;
    fProgress = 0.0f;
    bIsMoving = true;

    // Get starting offset based on direction
    // Entity is moving TO this tile FROM the neighboring tile
    int16_t startX, startY;
    GetDirectionStartOffset(direction, startX, startY);
    fStartOffsetX = static_cast<float>(startX);
    fStartOffsetY = static_cast<float>(startY);

    // Initial position is at the start offset
    fCurrentOffsetX = fStartOffsetX;
    fCurrentOffsetY = fStartOffsetY;
}

//=============================================================================
// StartMoveWithOffset - Begin movement with custom initial offset
// Used for seamless tile transitions during continuous movement
// The starting offset can be outside normal [-32, 0] range to ensure visual continuity
//=============================================================================
void EntityMotion::StartMoveWithOffset(int8_t direction, uint32_t currentTime, uint32_t duration, float offsetX, float offsetY)
{
    cDirection = direction;
    dwStartTime = currentTime;
    bIsMoving = true;

    // Use custom start offsets (allows values like -33.6 for seamless transitions)
    fStartOffsetX = offsetX;
    fStartOffsetY = offsetY;

    // Adjust duration proportionally to the extra distance
    // If covering 33.6 pixels instead of 32, take proportionally longer
    float normalDist = 32.0f;
    float actualDistX = std::abs(offsetX);
    float actualDistY = std::abs(offsetY);
    float actualDist = std::max(actualDistX, actualDistY);
    if (actualDist > 0.0f && actualDist != normalDist) {
        float scale = actualDist / normalDist;
        dwDuration = static_cast<uint32_t>(duration * scale);
    } else {
        dwDuration = duration;
    }

    fProgress = 0.0f;

    // Initial position at the custom offset
    fCurrentOffsetX = offsetX;
    fCurrentOffsetY = offsetY;
}

//=============================================================================
// QueueMove - Queue a follow-up movement for seamless chaining
//=============================================================================
void EntityMotion::QueueMove(int8_t direction, uint32_t duration)
{
    bHasPending = true;
    cPendingDirection = direction;
    dwPendingDuration = duration;
}

//=============================================================================
// Update - Update interpolation each frame
//=============================================================================
void EntityMotion::Update(uint32_t currentTime)
{
    if (!bIsMoving) return;
    if (dwDuration == 0) {
        // Prevent division by zero
        Stop();
        return;
    }

    // Calculate elapsed time
    uint32_t elapsed = currentTime - dwStartTime;

    // Calculate progress (0.0 to 1.0)
    fProgress = static_cast<float>(elapsed) / static_cast<float>(dwDuration);

    // Check for completion
    if (fProgress >= 1.0f) {
        if (bHasPending) {
            // Chain into next movement seamlessly
            // Overshoot time carries into next movement
            uint32_t overshoot = elapsed - dwDuration;
            int8_t pendDir = cPendingDirection;
            uint32_t pendDur = dwPendingDuration;
            bHasPending = false;
            StartMove(pendDir, currentTime - overshoot, pendDur);
            // Recursively update with remaining time
            Update(currentTime);
            return;
        }
        else {
            // No pending: snap to tile center
            fProgress = 1.0f;
            bIsMoving = false;
            fCurrentOffsetX = 0.0f;
            fCurrentOffsetY = 0.0f;
            return;
        }
    }

    // Linear interpolation from start offset to (0, 0)
    // Formula: current = start * (1 - progress)
    // Keep as float for smooth sub-pixel interpolation
    float invProgress = 1.0f - fProgress;
    fCurrentOffsetX = fStartOffsetX * invProgress;
    fCurrentOffsetY = fStartOffsetY * invProgress;
}

//=============================================================================
// Stop - Stop movement at current position
//=============================================================================
void EntityMotion::Stop()
{
    bIsMoving = false;
    // Keep current offset - don't snap
}

//=============================================================================
// Bump - Hard snap back to tile center (collision)
//=============================================================================
void EntityMotion::Bump()
{
    // Classic bump behavior - immediate snap to tile center
    bIsMoving = false;
    fProgress = 0.0f;
    fCurrentOffsetX = 0.0f;
    fCurrentOffsetY = 0.0f;
    fStartOffsetX = 0.0f;
    fStartOffsetY = 0.0f;
    bHasPending = false;
}

//=============================================================================
// Reset - Reset to default state
//=============================================================================
void EntityMotion::Reset()
{
    bIsMoving = false;
    cDirection = 0;
    fProgress = 0.0f;
    dwStartTime = 0;
    dwDuration = 0;
    fStartOffsetX = 0.0f;
    fStartOffsetY = 0.0f;
    fCurrentOffsetX = 0.0f;
    fCurrentOffsetY = 0.0f;
    bHasPending = false;
    cPendingDirection = 0;
    dwPendingDuration = 0;
}

//=============================================================================
// GetDirectionStartOffset - Get pixel offset for movement start position
//
// When an entity moves in direction D, they came FROM the opposite tile.
// This returns the pixel offset from the current tile's center where
// the entity should START their interpolation.
//
// Direction mapping:
//   1 = North     (entity came from South)    -> offset (0, +32)
//   2 = NorthEast (entity came from SouthWest)-> offset (-32, +32)
//   3 = East      (entity came from West)     -> offset (-32, 0)
//   4 = SouthEast (entity came from NorthWest)-> offset (-32, -32)
//   5 = South     (entity came from North)    -> offset (0, -32)
//   6 = SouthWest (entity came from NorthEast)-> offset (+32, -32)
//   7 = West      (entity came from East)     -> offset (+32, 0)
//   8 = NorthWest (entity came from SouthEast)-> offset (+32, +32)
//=============================================================================
void EntityMotion::GetDirectionStartOffset(int8_t direction, int16_t& outX, int16_t& outY)
{
    constexpr int16_t T = MovementTiming::TILE_SIZE;

    switch (direction) {
        case 1: // North - came from South
            outX = 0;
            outY = T;
            break;
        case 2: // NorthEast - came from SouthWest
            outX = -T;
            outY = T;
            break;
        case 3: // East - came from West
            outX = -T;
            outY = 0;
            break;
        case 4: // SouthEast - came from NorthWest
            outX = -T;
            outY = -T;
            break;
        case 5: // South - came from North
            outX = 0;
            outY = -T;
            break;
        case 6: // SouthWest - came from NorthEast
            outX = T;
            outY = -T;
            break;
        case 7: // West - came from East
            outX = T;
            outY = 0;
            break;
        case 8: // NorthWest - came from SouthEast
            outX = T;
            outY = T;
            break;
        default:
            outX = 0;
            outY = 0;
            break;
    }
}

//=============================================================================
// GetDurationForAction - Get movement duration for an action type
//=============================================================================
uint32_t EntityMotion::GetDurationForAction(int action, bool hasHaste, bool isFrozen)
{
    uint32_t baseDuration;

    switch (action) {
        case DEF_OBJECTMOVE:
            baseDuration = MovementTiming::WALK_DURATION_MS;
            break;
        case DEF_OBJECTRUN:
            baseDuration = MovementTiming::RUN_DURATION_MS;
            break;
        case DEF_OBJECTDAMAGEMOVE:
            baseDuration = MovementTiming::DAMAGE_MOVE_DURATION_MS;
            break;
        case DEF_OBJECTATTACKMOVE:
            baseDuration = MovementTiming::ATTACK_MOVE_DURATION_MS;
            break;
        default:
            baseDuration = MovementTiming::WALK_DURATION_MS;
            break;
    }

    // Apply status modifiers
    if (hasHaste) {
        // Haste speeds up movement by ~30%
        baseDuration = static_cast<uint32_t>(baseDuration * 0.7f);
    }
    if (isFrozen) {
        // Frozen slows movement by ~25%
        baseDuration = static_cast<uint32_t>(baseDuration * 1.25f);
    }

    return baseDuration;
}
