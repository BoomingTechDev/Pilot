#include "runtime/function/framework/component/motor/motor_component.h"

#include "runtime/core/base/macro.h"
#include "runtime/core/base/public_singleton.h"

#include "runtime/engine.h"
#include "runtime/function/controller/character_controller.h"
#include "runtime/function/framework/component/camera/camera_component.h"
#include "runtime/function/framework/component/transform/transform_component.h"
#include "runtime/function/framework/level/level.h"
#include "runtime/function/framework/object/object.h"
#include "runtime/function/framework/world/world_manager.h"
#include "runtime/function/input/input_system.h"

namespace Pilot
{
    MotorComponent::MotorComponent(const MotorRes& motor_param, GObject* parent_object) :
        Component(parent_object), m_move_speed(motor_param.m_move_speed)
    {
        m_motor_res.m_move_speed  = motor_param.m_move_speed;
        m_motor_res.m_jump_height = motor_param.m_jump_height;
        if (motor_param.m_controller_config.getTypeName() == "PhysicsControllerConfig")
        {
            auto controller_config                            = new PhysicsControllerConfig;
            m_motor_res.m_controller_config.getPtrReference() = controller_config;
            *controller_config = *static_cast<PhysicsControllerConfig*>(motor_param.m_controller_config.operator->());

            m_motor_res.m_controller_type = ControllerType::physics;
            m_controller                  = new CharacterController(controller_config->m_capsule_shape);
        }
        else if (motor_param.m_controller_config != nullptr)
        {
            m_motor_res.m_controller_type = ControllerType::invalid;
            LOG_ERROR("invalid controller type, not able to move");
        }
    }

    MotorComponent::~MotorComponent()
    {
        if (m_motor_res.m_controller_type == ControllerType::physics)
        {
            delete m_controller;
            m_controller = nullptr;
        }
    }

    void MotorComponent::tick(float delta_time)
    {
        if ((m_tick_in_editor_mode == false) && g_is_editor_mode)
            return;

        tickPlayerMotor(delta_time);
    }

    void MotorComponent::tickPlayerMotor(float delta_time)
    {
        TransformComponent* transform_component =
            m_parent_object->tryGetComponent<TransformComponent>("TransformComponent");

        Radian turn_angle_yaw = InputSystem::getInstance().m_cursor_delta_yaw;

        unsigned int command = InputSystem::getInstance().getGameCommand();

        if (command >= (unsigned int)GameCommand::invalid)
            return;

        calculatedDesiredMoveSpeed(command, delta_time);
        calculatedDesiredMoveDirection(command, transform_component->getRotation());
        calculateDesiredDisplacement(delta_time);

        setTargetPosition(transform_component->getPosition());
        transform_component->setPosition(m_target_position);
    }

    void MotorComponent::calculatedDesiredMoveSpeed(unsigned int command, float delta_time)
    {
        m_move_speed_ratio = 1.0f;
        if ((unsigned int)GameCommand::sprint & command)
        {
            m_move_speed_ratio = 2.f;
        }

        Level* level = WorldManager::getInstance().getCurrentActiveLevel();
        if (level == nullptr)
            return;

        const float gravity = level->getGravity();

        if (m_jump_state == JumpState::idle)
        {
            if ((unsigned int)GameCommand::jump & command)
            {
                m_jump_state                  = JumpState::rising;
                m_vertical_move_speed         = Math::sqrt(m_motor_res.m_jump_height * 2 * gravity);
                m_jump_horizontal_speed_ratio = m_move_speed_ratio;
            }
            else
            {
                m_vertical_move_speed = 0.f;
                m_jump_state          = JumpState::idle;
            }
        }
        else if (m_jump_state == JumpState::rising || m_jump_state == JumpState::falling)
        {
            m_vertical_move_speed -= gravity * delta_time;
            if (m_vertical_move_speed <= 0.f)
            {
                m_jump_state = JumpState::falling;
            }
        }
    }

    void MotorComponent::calculatedDesiredMoveDirection(unsigned int command, const Quaternion& object_rotation)
    {
        if (m_jump_state == JumpState::idle)
        {
            Vector3 forward_dir = object_rotation * Vector3::NEGATIVE_UNIT_Y;
            Vector3 left_dir    = object_rotation * Vector3::UNIT_X;

            m_desired_horizontal_move_direction = Vector3::ZERO;
            if ((unsigned int)GameCommand::forward & command)
            {
                m_desired_horizontal_move_direction += forward_dir;
            }

            if ((unsigned int)GameCommand::backward & command)
            {
                m_desired_horizontal_move_direction -= forward_dir;
            }

            if ((unsigned int)GameCommand::left & command)
            {
                m_desired_horizontal_move_direction += left_dir;
            }

            if ((unsigned int)GameCommand::right & command)
            {
                m_desired_horizontal_move_direction -= left_dir;
            }

            m_desired_horizontal_move_direction.normalise();
        }
    }

    void MotorComponent::calculateDesiredDisplacement(float delta_time)
    {
        if (m_jump_state == JumpState::idle)
        {
            m_desired_displacement =
                m_desired_horizontal_move_direction * m_move_speed * m_move_speed_ratio * delta_time +
                Vector3::UNIT_Z * m_vertical_move_speed * delta_time;
        }
        else
        {
            m_desired_displacement =
                m_desired_horizontal_move_direction * m_move_speed * m_jump_horizontal_speed_ratio * delta_time +
                Vector3::UNIT_Z * m_vertical_move_speed * delta_time;
        }
    }

    void MotorComponent::setTargetPosition(const Vector3&& current_position)
    {
        Vector3 final_position = current_position;

        switch (m_motor_res.m_controller_type)
        {
            case ControllerType::none:
                final_position += m_desired_displacement;
                break;
            case ControllerType::physics:
                final_position = m_controller->move(final_position, m_desired_displacement);
                break;
            default:
                break;
        }

        // Pilot-hack: motor level simulating jump, character always above z-plane
        if (m_jump_state == JumpState::falling && m_target_position.z <= 0.f)
        {
            final_position.z = 0.f;
            m_jump_state     = JumpState::idle;
        }

        m_target_position = final_position;
    }

} // namespace Pilot
