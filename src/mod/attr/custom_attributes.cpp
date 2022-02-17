#include "mod.h"
#include "stub/baseentity.h"
#include "stub/econ.h"
#include "stub/extraentitydata.h"
#include "stub/tfplayer.h"
#include "stub/projectiles.h"
#include "stub/tfweaponbase.h"
#include "stub/objects.h"
#include "stub/entities.h"
#include "stub/gamerules.h"
#include "stub/usermessages_sv.h"
#include "stub/particles.h"
#include "stub/misc.h"
#include "stub/trace.h"
#include "stub/upgrades.h"
#include "mod/pop/common.h"
#include "mod/pop/popmgr_extensions.h"
#include "util/iterate.h"
#include <gamemovement.h>
#include <boost/tokenizer.hpp>


class CDmgAccumulator;

class IPredictionSystem
{
public:
	void *vtable;
	IPredictionSystem	*m_pNextSystem;
	int				m_bSuppressEvent;
	CBaseEntity			*m_pSuppressHost;

	int					m_nStatusPushed;
};

namespace Mod::Attr::Custom_Attributes
{

	std::set<std::string> precached;

	GlobalThunk<void *> g_pFullFileSystem("g_pFullFileSystem");
	GlobalThunk<IPredictionSystem> g_RecipientFilterPredictionSystem("g_RecipientFilterPredictionSystem");

	enum FastAttributeClassPlayer
	{
		STOMP_BUILDING_DAMAGE,
		STOMP_PLAYER_TIME,
		STOMP_PLAYER_DAMAGE,
		STOMP_PLAYER_FORCE,
		NOT_SOLID_TO_PLAYERS,
		MULT_MAX_OVERHEAL_SELF,
		MIN_RESPAWN_TIME,
		MULT_STEP_HEIGHT,
		IGNORE_PLAYER_CLIP,
		ALLOW_BUNNY_HOP,
		ATTRIB_COUNT_PLAYER,
	};
	enum FastAttributeClassItem
	{
		ALWAYS_CRIT,
		ADD_COND_ON_ACTIVE,
		MAX_AOE_TARGETS,
		DUCK_ACCURACY_MULT,
		CONTINOUS_ACCURACY_MULT,
		CONTINOUS_ACCURACY_TIME,
		CONTINOUS_ACCURACY_TIME_RECOVERY,
		MOVE_ACCURACY_MULT,
		ALT_FIRE_DISABLED,
		ATTRIB_COUNT_ITEM,
	};
	const char *fast_attribute_classes_player[ATTRIB_COUNT_PLAYER] = {
		"stomp_building_damage", 
		"stomp_player_time", 
		"stomp_player_damage", 
		"stomp_player_force", 
		"not_solid_to_players",
		"mult_max_ovelheal_self",
		"min_respawn_time",
		"mult_step_height",
		"ignore_player_clip",
		"allow_bunny_hop"
	};

	const char *fast_attribute_classes_item[ATTRIB_COUNT_ITEM] = {
		"always_crit",
		"add_cond_when_active",
		"max_aoe_targets",
		"duck_accuracy_mult",
		"continous_accuracy_mult",
		"continous_accuracy_time",
		"continous_accuracy_time_recovery",
		"move_accuracy_mult",
		"unimplemented_altfire_disabled"
	};

	float *fast_attribute_cache[2048];


	CBaseEntity *last_fast_attrib_entity = nullptr;
	float *CreateNewAttributeCache(CBaseEntity *entity) {

		if (rtti_cast<IHasAttributes *>(entity) == nullptr) return nullptr;
		
		int count = entity->IsPlayer() ? ATTRIB_COUNT_PLAYER : ATTRIB_COUNT_ITEM;
		float *attrib_cache = new float[count];
		
		for(int i = 0; i < count; i++) {
			attrib_cache[i] = FLT_MIN;
		}

		//GetExtraEntityDataWithAttributes(entity)->fast_attribute_cache = attrib_cache;
		fast_attribute_cache[ENTINDEX(entity)] = attrib_cache;
		return attrib_cache;
	}

	float SetAttributeCacheEntry(CBaseEntity *entity, float value, int name, float *attrib_cache) {
		CAttributeManager *mgr = nullptr;
		if (entity->IsPlayer()) {
            mgr = reinterpret_cast<CTFPlayer *>(entity)->GetAttributeManager();
        }
        else if (entity->IsBaseCombatWeapon() || entity->IsWearable()) {
            mgr = reinterpret_cast<CEconEntity *>(entity)->GetAttributeManager();
        }
        if (mgr == nullptr)
            return value;

		float result = mgr->ApplyAttributeFloat(value, entity, AllocPooledString_StaticConstantStringPointer(entity->IsPlayer() ? fast_attribute_classes_player[name] : fast_attribute_classes_item[name]));
		attrib_cache[name] = result;
		return result;
	}

	// Fast Attribute Cache, for every tick attribute querying. The value parameter must be a static value, unlike the CALL_ATTRIB_HOOK_ calls;
	inline float GetFastAttributeFloat(CBaseEntity *entity, float value, int name) {
		
		if (entity == nullptr)
			return value;

		//auto data = static_cast<ExtraEntityDataWithAttributes *>(entity->GetExtraEntityData());
		float *attrib_cache = fast_attribute_cache[ENTINDEX(entity)];
		if (attrib_cache == nullptr && (attrib_cache = CreateNewAttributeCache(entity)) == nullptr) {
			return value;
		}
		
		float result = attrib_cache[name];
			
		if (result != FLT_MIN) {
			return result;
		}

		return SetAttributeCacheEntry(entity, value, name, attrib_cache);
	}

	inline int GetFastAttributeInt(CBaseEntity *entity, int value, int name) {
		return RoundFloatToInt(GetFastAttributeFloat(entity, value, name));
	}

#define GET_STRING_ATTRIBUTE(attrlist, name, varname) \
	static int inddef_##varname = GetItemSchema()->GetAttributeDefinitionByName(name)->GetIndex(); \
	const char * varname = GetStringAttribute(attrlist, inddef_##varname);

	
	const char *GetStringAttribute(CAttributeList &attrlist, int index) {
		auto attr = attrlist.GetAttributeByID(index);
		const char *value = nullptr;
		if (attr != nullptr && attr->GetValuePtr()->m_String != nullptr) {
			CopyStringAttributeValueToCharPointerOutput(attr->GetValuePtr()->m_String, &value);
		}
		return value;
	}

	const char *GetStringAttribute(CAttributeList &attrlist, const char* name) {
		auto attr = attrlist.GetAttributeByName(name);
		const char *value = nullptr;
		if (attr != nullptr && attr->GetValuePtr()->m_String != nullptr) {
			CopyStringAttributeValueToCharPointerOutput(attr->GetValuePtr()->m_String, &value);
		}
		return value;
	}

	inline void PrecacheSound(const char *name) {
		if (name != nullptr && name[0] != '\0' && precached.count(name) == 0) {
			if (!enginesound->PrecacheSound(name, true))
				CBaseEntity::PrecacheScriptSound(name);
			precached.insert(name);
		}
	}

	DETOUR_DECL_MEMBER(bool, CTFPlayer_CanAirDash)
	{
		bool ret = DETOUR_MEMBER_CALL(CTFPlayer_CanAirDash)();
		if (!ret) {
			auto player = reinterpret_cast<CTFPlayer *>(this);
			if (!player->IsPlayerClass(TF_CLASS_SCOUT)) {
				int dash = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER( player, dash, air_dash_count );
				if (dash > player->m_Shared->m_iAirDash)
					return true;
			}
		}
		return ret;
	}

	DETOUR_DECL_MEMBER(bool, CWeaponMedigun_AllowedToHealTarget, CBaseEntity *target)
	{
		bool ret = DETOUR_MEMBER_CALL(CWeaponMedigun_AllowedToHealTarget)(target);
		if (!ret && target != nullptr && target->IsBaseObject()) {
			auto medigun = reinterpret_cast<CWeaponMedigun *>(this);
			auto owner = medigun->GetOwnerEntity();
			
			if (owner != nullptr && target->GetTeamNumber() == owner->GetTeamNumber()) {
				int can_heal = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER( medigun, can_heal, medic_machinery_beam );
				return can_heal != 0;
			}
			
		}
		return ret;
	}

	DETOUR_DECL_MEMBER(void, CWeaponMedigun_HealTargetThink)
	{
		DETOUR_MEMBER_CALL(CWeaponMedigun_HealTargetThink)();
		auto medigun = reinterpret_cast<CWeaponMedigun *>(this);
		CBaseEntity *healobject = medigun->GetHealTarget();
		if (healobject != nullptr && healobject->IsBaseObject() && healobject->GetHealth() < healobject->GetMaxHealth() ) {
			int can_heal = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( medigun, can_heal, medic_machinery_beam );
			auto object = ToBaseObject(healobject);
			object->SetHealth( object->GetHealth() + ( (medigun->GetHealRate() / 10.f) * can_heal ) );
			
		}
	}


	void CreateExtraArrow(CTFCompoundBow *bow, CTFProjectile_Arrow *main_arrow, const QAngle& angles, float speed) {
		CTFProjectile_Arrow* pExtraArrow = CTFProjectile_Arrow::Create( main_arrow->GetAbsOrigin(), angles, speed, bow->GetProjectileGravity(), bow->GetWeaponProjectileType(), main_arrow->GetOwnerEntity(), main_arrow->GetOwnerEntity() );
		if ( pExtraArrow != nullptr )
		{
			pExtraArrow->SetLauncher( bow );
			bool critical = main_arrow->m_bCritical;
			pExtraArrow->m_bCritical = critical;
			pExtraArrow->SetDamage( (TFGameRules()->IsPVEModeControlled(bow->GetOwnerEntity()) ? 1.0f : 0.5f) * bow->GetProjectileDamage() );
			//if ( main_arrow->CanPenetrate() )
			//{
				//pExtraArrow->SetPenetrate( true );
			//}
			pExtraArrow->SetCollisionGroup( main_arrow->GetCollisionGroup() );
		}
	}

	float GetRandomSpreadOffset( CTFCompoundBow *bow, int iLevel, float angle )
	{
		float flMaxRandomSpread = 2.5f;// sv_arrow_max_random_spread_angle.GetFloat();
		float flRandom = RemapValClamped( bow->GetCurrentCharge(), 0.f, bow->GetChargeMaxTime(), RandomFloat( -flMaxRandomSpread, flMaxRandomSpread ), 0.f );
		return angle/*sv_arrow_spread_angle.GetFloat()*/ * iLevel + flRandom;
	}

	CBaseAnimating *projectile_arrow = nullptr;

	bool force_send_client = false;

	void AttackEnemyProjectiles( CTFPlayer *player, CTFWeaponBase *weapon, int shoot_projectiles)
	{

		const int nSweepDist = 300;	// How far out
		const int nHitDist = ( player->IsMiniBoss() ) ? 56 : 38;	// How far from the center line (radial)

		// Pos
		const Vector &vecGunPos = ( player->IsMiniBoss() ) ? player->Weapon_ShootPosition() : player->EyePosition();
		Vector vecForward;
		AngleVectors( weapon->GetAbsAngles(), &vecForward );
		Vector vecGunAimEnd = vecGunPos + vecForward * (float)nSweepDist;

		// Iterate through each grenade/rocket in the sphere
		const int maxCollectedEntities = 128;
		CBaseEntity	*pObjects[ maxCollectedEntities ];
		
		CFlaggedEntitiesEnum iter = CFlaggedEntitiesEnum(pObjects, maxCollectedEntities, FL_GRENADE );

		partition->EnumerateElementsInSphere(PARTITION_ENGINE_NON_STATIC_EDICTS, vecGunPos, nSweepDist, false, &iter);

		int count = iter.GetCount();

		for ( int i = 0; i < count; i++ )
		{
			if ( player->GetTeamNumber() == pObjects[i]->GetTeamNumber() )
				continue;

			// Hit?
			const Vector &vecGrenadePos = pObjects[i]->GetAbsOrigin();
			float flDistToLine = CalcDistanceToLineSegment( vecGrenadePos, vecGunPos, vecGunAimEnd );
			if ( flDistToLine <= nHitDist )
			{
				if ( player->FVisible( pObjects[i], MASK_SOLID ) == false )
					continue;

				if ( ( pObjects[i]->GetFlags() & FL_ONGROUND ) )
					continue;
					
				if ( !pObjects[i]->IsDeflectable() )
					continue;

				CBaseProjectile *pProjectile = static_cast< CBaseProjectile* >( pObjects[i] );
				if ( pProjectile->ClassMatches("tf_projectile*") && pProjectile->IsDestroyable() )
				{
					pProjectile->Destroy( false, true );

					weapon->EmitSound( "Halloween.HeadlessBossAxeHitWorld" );
				}
			}
		}
	}

	CBaseAnimating *SpawnCustomProjectile(const char *name, CTFWeaponBaseGun *weapon, CTFPlayer *player)
	{
		CBaseAnimating *retval = nullptr;
		
		Vector vecSrc;
		QAngle angForward;
		Vector vecOffset( 23.5f, 12.0f, -3.0f );
		if ( player->GetFlags() & FL_DUCKING )
		{
			vecOffset.z = 8.0f;
		}
		weapon->GetProjectileFireSetup( player, vecOffset, &vecSrc, &angForward, false ,2000);

		if (strcmp(name, "mechanicalarmorb") == 0) {

			auto projectile = rtti_cast<CTFProjectile_MechanicalArmOrb *>(CBaseEntity::CreateNoSpawn("tf_projectile_mechanicalarmorb", vecSrc, player->EyeAngles(), player));
			if (projectile != nullptr) {
				projectile->SetOwnerEntity(player);
				projectile->SetLauncher   (weapon);
				
				float mult_speed = 1.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, mult_speed, mult_projectile_speed);

				Vector eye_angles_fwd;
				AngleVectors(angForward, &eye_angles_fwd);
				projectile->SetAbsVelocity(mult_speed * 700.0f * eye_angles_fwd);
				
				projectile->ChangeTeam(player->GetTeamNumber());
				
				if (projectile->IsCritical()) {
					projectile->SetCritical(false);
				}
				
				DispatchSpawn(projectile);
			}
		}
		
		if (weapon->ShouldPlayFireAnim()) {
			player->DoAnimationEvent(PLAYERANIMEVENT_ATTACK_PRIMARY);
		}
		
		weapon->RemoveProjectileAmmo(player);
		weapon->m_flLastFireTime = gpGlobals->curtime;
		weapon->DoFireEffects();
		weapon->UpdatePunchAngles(player);
		
		if (player->m_Shared->IsStealthed() && weapon->ShouldRemoveInvisibilityOnPrimaryAttack()) {
			player->RemoveInvisibility();
		}
		return retval;
	}	

	THINK_FUNC_DECL(ProjectileLifetime) {
		this->Remove();
	};

	THINK_FUNC_DECL(ProjectileSoundDelay) {
		this->Remove();
	};

	bool fire_projectile_multi = true;
	int old_clip = 0;

	DETOUR_DECL_MEMBER(CBaseAnimating *, CTFWeaponBaseGun_FireProjectile, CTFPlayer *player)
	{
		auto weapon = reinterpret_cast<CTFWeaponBaseGun *>(this);

		int attr_projectile_count = 1;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, attr_projectile_count, mult_projectile_count);

		int attr_fire_all_at_once = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, attr_fire_all_at_once, fire_full_clip_at_once);

		int num_shots = attr_projectile_count;
		if (attr_fire_all_at_once != 0) {
			attr_projectile_count *= (weapon->IsEnergyWeapon() ? (weapon->Energy_GetMaxEnergy() / weapon->Energy_GetShotCost()) : weapon->m_iClip1);
		}

		CBaseAnimating *proj = nullptr;
		//int seed = CBaseEntity::GetPredictionRandomSeed() & 255;
		for (int i = 0; i < attr_projectile_count; i++) {

			GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "override projectile type extra", projectilename);

			fire_projectile_multi = (i % num_shots) == 0;

			//if (i != 0) {
			//	RandomSeed(gpGlobals->tickcount + i);
			//}

			if (projectilename != nullptr) {
				proj = SpawnCustomProjectile(projectilename, weapon, player);
				
			}
			else {
				proj = DETOUR_MEMBER_CALL(CTFWeaponBaseGun_FireProjectile)(player);
			}
			
			fire_projectile_multi = true;

			if (proj != nullptr) {
				float attr_lifetime = 0.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, attr_lifetime, projectile_lifetime);

				if (attr_lifetime != 0.0f) {
					THINK_FUNC_SET(proj, ProjectileLifetime, gpGlobals->curtime + attr_lifetime);
					//proj->ThinkSet(&ProjectileLifetime::Update, gpGlobals->curtime + attr_lifetime, "ProjLifetime");
				}
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "projectile trail particle", particlename);
				if (particlename != nullptr) {

					force_send_client = true;
					if (*particlename == '~') {
						StopParticleEffects(proj);
						DispatchParticleEffect(particlename + 1, PATTACH_ABSORIGIN_FOLLOW, proj, INVALID_PARTICLE_ATTACHMENT, false);
					} else {
						DispatchParticleEffect(particlename, PATTACH_ABSORIGIN_FOLLOW, proj, INVALID_PARTICLE_ATTACHMENT, false);
					}
					force_send_client = false;
				}
				if (i < attr_projectile_count - 1)
					weapon->ModifyProjectile(proj);
				
				IPhysicsObject *physics = proj->VPhysicsGetObject();

				float gravity = 0.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, gravity, projectile_gravity_native);
				if (gravity != 0.0f) {
					proj->SetGravity(gravity);
				}
				
				if (physics != nullptr) {
					if (gravity != 0.0f) {
						physics->EnableGravity(false);
					}
					float bounce_speed = 0.0f;
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, bounce_speed, grenade_bounce_speed);
					if (bounce_speed != 0.0f) {
						physics->SetInertia({10000.0f,10000.0f,10000.0f});
					}
					int drag = 0;
					CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, drag, grenade_no_drag);
					if (drag != 0) {
						physics->EnableDrag(false);
					}
				}

				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "projectile sound", soundname);
				if (soundname != nullptr) {
					PrecacheSound(soundname);
					proj->EmitSound(soundname);
				}
			}
		}
		
		int shoot_projectiles = 0;
			
		CALL_ATTRIB_HOOK_INT_ON_OTHER(player, shoot_projectiles, attack_projectiles);
		if (shoot_projectiles > 0) {
			auto weapon = reinterpret_cast<CTFWeaponBase*>(this);
			if (weapon->GetWeaponID() != TF_WEAPON_MINIGUN)
				AttackEnemyProjectiles(player, reinterpret_cast<CTFWeaponBase*>(this), shoot_projectiles);
		}
		old_clip = 0;
		projectile_arrow = proj;
		return proj;
	}

	DETOUR_DECL_MEMBER(void, CTFWeaponBaseGun_RemoveProjectileAmmo, CTFPlayer *player)
	{
		if (fire_projectile_multi)
			DETOUR_MEMBER_CALL(CTFWeaponBaseGun_RemoveProjectileAmmo)(player);
	}

	DETOUR_DECL_MEMBER(void, CTFWeaponBaseGun_UpdatePunchAngles, CTFPlayer *player)
	{
		if (fire_projectile_multi)
			DETOUR_MEMBER_CALL(CTFWeaponBaseGun_UpdatePunchAngles)(player);
	}
	
	DETOUR_DECL_MEMBER(void, CTFCompoundBow_LaunchGrenade)
	{
		DETOUR_MEMBER_CALL(CTFCompoundBow_LaunchGrenade)();

		int attib_arrow_mastery = 0;
		auto bow = reinterpret_cast<CTFCompoundBow *>(this);
		CALL_ATTRIB_HOOK_INT_ON_OTHER( bow, attib_arrow_mastery, arrow_mastery );

		if (attib_arrow_mastery != 0 && projectile_arrow != nullptr) {

			Vector vecMainVelocity = projectile_arrow->GetAbsVelocity();
			float flMainSpeed = vecMainVelocity.Length();

			CTFProjectile_Arrow *arrow = static_cast<CTFProjectile_Arrow *>(projectile_arrow);

			float angle = attib_arrow_mastery > 0 ? 5.0f : - 360.0f / (attib_arrow_mastery - 1);

			int count = attib_arrow_mastery > 0 ? attib_arrow_mastery : -attib_arrow_mastery;
			if (arrow != nullptr) {
				for (int i = 0; i < count; i++) {
					
					QAngle qOffset1 = projectile_arrow->GetAbsAngles() + QAngle( 0, GetRandomSpreadOffset( bow, i + 1, angle ), 0 );
					CreateExtraArrow( bow, arrow, qOffset1, flMainSpeed );
					if (attib_arrow_mastery > 0 ) {
						QAngle qOffset2 = projectile_arrow->GetAbsAngles() + QAngle( 0, -GetRandomSpreadOffset( bow, i + 1, angle ), 0 );
						CreateExtraArrow( bow, arrow, qOffset2, flMainSpeed );
					}

				}
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CTFWeaponBaseMelee_Swing, CTFPlayer *player)
	{
		DETOUR_MEMBER_CALL(CTFWeaponBaseMelee_Swing)(player);
		auto weapon = reinterpret_cast<CTFWeaponBaseMelee*>(this);
		float smacktime = weapon->m_flSmackTime;
		float time = gpGlobals->curtime;
		float attr_smacktime = 1.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( weapon, attr_smacktime, mult_smack_time);
		weapon->m_flSmackTime = time + (smacktime - time) * attr_smacktime;
		if (!player->IsBot() && weapon->m_flSmackTime > weapon->m_flNextPrimaryAttack)
			weapon->m_flSmackTime = weapon->m_flNextPrimaryAttack - 0.02;
	}

	DETOUR_DECL_MEMBER(int, CBaseObject_OnTakeDamage, CTakeDamageInfo &info)
	{
		int damage = DETOUR_MEMBER_CALL(CBaseObject_OnTakeDamage)(info);
		auto weapon = info.GetWeapon();
		if (weapon != nullptr) {
			auto tfweapon = ToBaseCombatWeapon(weapon);
			if (tfweapon != nullptr) {
				float attr_disabletime = 0.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( tfweapon, attr_disabletime, disable_buildings_on_hit);
				if (attr_disabletime > 0.0f) {
					reinterpret_cast<CBaseObject*>(this)->SetPlasmaDisabled(attr_disabletime);
				}
			}
		}
		return damage;
	}

	CBaseEntity *killer_weapon = nullptr;
	bool is_ice = false;
	DETOUR_DECL_MEMBER(void, CTFPlayer_Event_Killed, const CTakeDamageInfo& info)
	{
		auto player = reinterpret_cast<CTFPlayer *>(this);

		if (info.GetWeapon() != nullptr) {
			int attr_ice = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( info.GetWeapon(), attr_ice, set_turn_to_ice );
			is_ice = attr_ice != 0;
		}
		else
			is_ice = false;
			
		killer_weapon = info.GetWeapon();
		DETOUR_MEMBER_CALL(CTFPlayer_Event_Killed)(info);
		killer_weapon = nullptr;

		ForEachTFPlayerEconEntity(player, [&](CEconEntity *entity){
			static int attachment_name_def = GetItemSchema()->GetAttributeDefinitionByName("attachment name")->GetIndex();
			if (entity->GetItem() != nullptr && entity->GetItem()->GetAttributeList().GetAttributeByID(attachment_name_def) != nullptr) {
				entity->AddEffects(EF_NODRAW);
			}
		});
		is_ice = false;
	}

	DETOUR_DECL_MEMBER(void, CTFPlayer_CreateRagdollEntity, bool bShouldGib, bool bBurning, bool bUberDrop, bool bOnGround, bool bYER, bool bGold, bool bIce, bool bAsh, int iCustom, bool bClassic)
	{
		bIce |= is_ice;
		
		return DETOUR_MEMBER_CALL(CTFPlayer_CreateRagdollEntity)(bShouldGib, bBurning, bUberDrop, bOnGround, bYER, bGold, bIce, bAsh, iCustom, bClassic);
	}

	void GetExplosionParticle(CTFPlayer *player, CTFWeaponBase *weapon, int &particle) {
		int no_particle = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, no_particle, no_explosion_particles);
		if (no_particle) {
			particle = -1;
			return;
		}

		GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "explosion particle", particlename);
		if (particlename != nullptr) {
			if (precached.find(particlename) == precached.end()) {
				PrecacheParticleSystem(particlename);
				precached.insert(particlename);
			}
			particle = GetParticleSystemIndex(particlename);
			DevMsg("Expl part %s\n",particlename);
		}
	}

	int particle_to_use = 0;

	CTFWeaponBaseGun *stickbomb = nullptr;
	DETOUR_DECL_MEMBER(void, CTFStickBomb_Smack)
	{	
		stickbomb = reinterpret_cast<CTFWeaponBaseGun*>(this);
		particle_to_use = 0;
		GetExplosionParticle(stickbomb->GetTFPlayerOwner(), stickbomb,particle_to_use);
		DETOUR_MEMBER_CALL(CTFStickBomb_Smack)();
		stickbomb = nullptr;
		particle_to_use = 0;
	}

	RefCount rc_CTFGameRules_RadiusDamage;
	
	int hit_entities_explosive = 0;
	int hit_entities_explosive_max = 0;

	bool minicrit = false;
	DETOUR_DECL_MEMBER(void, CTFGameRules_RadiusDamage, CTFRadiusDamageInfo& info)
	{
		SCOPED_INCREMENT(rc_CTFGameRules_RadiusDamage);
		if (stickbomb != nullptr) {
			float radius = 1.0f;
			
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( stickbomb, radius, mult_explosion_radius);

			info.m_flRadius = radius * info.m_flRadius;

			int iLargeExplosion = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( stickbomb, iLargeExplosion, use_large_smoke_explosion );
			if ( iLargeExplosion > 0 )
			{
				force_send_client = true;
				DispatchParticleEffect( "explosionTrail_seeds_mvm", info.m_vecOrigin , vec3_angle );
				DispatchParticleEffect( "fluidSmokeExpl_ring_mvm", info.m_vecOrigin , vec3_angle);
				force_send_client = false;
			}
			//DevMsg("mini crit used: %d\n",minicrit);
			//info.m_DmgInfo->SetDamageType(info.m_DmgInfo->GetDamageType() & (~DMG_USEDISTANCEMOD));
			if (minicrit) {
				stickbomb->GetTFPlayerOwner()->m_Shared->AddCond(TF_COND_NOHEALINGDAMAGEBUFF, 99.0f);
				DETOUR_MEMBER_CALL(CTFGameRules_RadiusDamage)(info);
				stickbomb->GetTFPlayerOwner()->m_Shared->RemoveCond(TF_COND_NOHEALINGDAMAGEBUFF);
				return;
			}
		}
		
		CBaseEntity *weapon = info.m_DmgInfo->GetWeapon();
		hit_entities_explosive = 0;
		hit_entities_explosive_max = GetFastAttributeInt(weapon, 0, MAX_AOE_TARGETS);

		DETOUR_MEMBER_CALL(CTFGameRules_RadiusDamage)(info);
	}

	CBasePlayer *process_movement_player = nullptr;
	DETOUR_DECL_MEMBER(void, CTFGameMovement_ProcessMovement, CBasePlayer *player, void *data)
	{
		process_movement_player = player;
		DETOUR_MEMBER_CALL(CTFGameMovement_ProcessMovement)(player, data);
		process_movement_player = nullptr;
	}

	DETOUR_DECL_MEMBER(bool, CTFGameMovement_CheckJumpButton)
	{
		bool ret = DETOUR_MEMBER_CALL(CTFGameMovement_CheckJumpButton)();

		if (ret && process_movement_player != nullptr) {
			auto player = ToTFPlayer(process_movement_player);
			//CAttributeList *attrlist = player->GetAttributeList();
			//auto attr = attrlist->GetAttributeByName("custom jump particle");
			//if (attr != nullptr) {
			//	
			//	const char *particlename;
			//	CopyStringAttributeValueToCharPointerOutput(attr->GetValuePtr()->m_String, &particlename);
				//GetItemSchema()->GetAttributeDefinitionByName("custom jump particle")->ConvertValueToString(*(attr->GetValuePtr()),particlename,255);

			//	DevMsg ("jump %s\n", particlename);
			//	DispatchParticleEffect( particlename, PATTACH_POINT_FOLLOW, player, "foot_L" );
			//	DispatchParticleEffect( particlename, PATTACH_POINT_FOLLOW, player, "foot_R" );
			//}

			if (!player->IsBot()) {
				int attr_jump = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER( player, attr_jump, bot_custom_jump_particle );
				if (attr_jump) {
					const char *particlename = "rocketjump_smoke";
					force_send_client = true;
					DispatchParticleEffect( particlename, PATTACH_POINT_FOLLOW, player, "foot_L" );
					DispatchParticleEffect( particlename, PATTACH_POINT_FOLLOW, player, "foot_R" );
					force_send_client = false;
				}
			}
		}
		return ret;
	}

	struct CustomModelEntry
	{
		CHandle<CTFWeaponBase> weapon;
		CHandle<CTFWearable> wearable;
		CHandle<CTFWearable> wearable_vm;
		int model_index;
	};
	std::vector<CustomModelEntry> model_entries;

	void ApplyAttachmentAttributesToEntity(CTFPlayer *owner, CBaseAnimating *entity, CEconEntity *econEntity)
	{
		int color = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER( econEntity, color, item_color_rgb);
		if (color != 0) {
			entity->SetRenderColorR(color >> 16);
			entity->SetRenderColorG((color >> 8) & 255);
			entity->SetRenderColorB(color & 255);
		}
		int invisible = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER( econEntity, invisible, is_invisible);
		if (invisible != 0) {
			DevMsg("SetInvisible\n");
			servertools->SetKeyValue(entity, "rendermode", "10");
			entity->AddEffects(EF_NODRAW);
			entity->SetRenderColorA(0);
		}

		CAttributeList &attrlist = econEntity->GetItem()->GetAttributeList();

		GET_STRING_ATTRIBUTE(attrlist, "attachment name", attachmentname);

		if (owner != nullptr && attachmentname != nullptr) {
			int attachment = owner->LookupAttachment(attachmentname);
			entity->SetEffects(entity->GetEffects() & ~(EF_BONEMERGE));
			if (attachment > 0) {
				entity->SetParent(owner, attachment);
				
			}

			Vector pos = vec3_origin;
			QAngle ang = vec3_angle;
			float scale = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( econEntity, scale, attachment_scale);
			
			GET_STRING_ATTRIBUTE(attrlist, "attachment offset", offsetstr);
			GET_STRING_ATTRIBUTE(attrlist, "attachment angles", anglesstr);

			if (offsetstr != nullptr)
				sscanf(offsetstr, "%f %f %f", &pos.x, &pos.y, &pos.z);

			if (anglesstr != nullptr)
				sscanf(anglesstr, "%f %f %f", &ang.x, &ang.y, &ang.z);
				
			if (scale != 1.0f) {
				entity->SetModelScale(scale);
			}
			entity->SetLocalOrigin(pos);
			entity->SetLocalAngles(ang);
		}

	}

	void CreateWeaponWearables(CustomModelEntry &entry)
	{
		if (entry.wearable_vm == nullptr) {
			auto wearable_vm = static_cast<CTFWearable *>(CreateEntityByName("tf_wearable_vm"));
			wearable_vm->Spawn();
			wearable_vm->GiveTo(entry.weapon->GetTFPlayerOwner());
			wearable_vm->SetModelIndex(entry.model_index);
			wearable_vm->m_bValidatedAttachedEntity = true;

			entry.wearable_vm = wearable_vm;
		}

		if (entry.wearable == nullptr) {
			auto wearable = static_cast<CTFWearable *>(CreateEntityByName("tf_wearable"));
			wearable->Spawn();
			wearable->GiveTo(entry.weapon->GetTFPlayerOwner());
			wearable->SetModelIndex(entry.model_index);
			wearable->m_bValidatedAttachedEntity = true;
			ApplyAttachmentAttributesToEntity(entry.weapon->GetTFPlayerOwner(), wearable, entry.weapon);

			entry.wearable = wearable;
		}
	}

	DETOUR_DECL_MEMBER(void, CEconEntity_UpdateModelToClass)
	{
		DETOUR_MEMBER_CALL(CEconEntity_UpdateModelToClass)();
		auto entity = reinterpret_cast<CEconEntity *>(this);
		CAttributeList &attrlist = entity->GetItem()->GetAttributeList();

		GET_STRING_ATTRIBUTE(attrlist, "custom item model", modelname);
		
		auto owner = ToTFPlayer(entity->GetOwnerEntity());

		if (modelname != nullptr) {

			int model_index = CBaseEntity::PrecacheModel(modelname);
			auto weapon = static_cast<CTFWeaponBase *>(ToBaseCombatWeapon(entity));
			if (weapon != nullptr && owner != nullptr && !owner->IsFakeClient()) {
				entity->SetRenderMode(kRenderTransAlpha);
				entity->SetRenderColorA(0);
				weapon->m_bBeingRepurposedForTaunt = true;
				model_entries.push_back({weapon, nullptr, nullptr, model_index});
				CreateWeaponWearables(model_entries.back());
			}
			else {
				for (int i = 0; i < MAX_VISION_MODES; ++i) {
					entity->SetModelIndexOverride(i, model_index);
				}
			}
		}

		ApplyAttachmentAttributesToEntity(owner, entity, entity);
	}

	// Convert float attribute value to uint, so that warpaints display properly when given to bots 
	THINK_FUNC_DECL(WarpaintAttributeCorrection)
	{
		auto weapon = reinterpret_cast<CBaseCombatWeapon *>(this);
		if (weapon->GetItem() != nullptr) {
			auto attr = weapon->GetItem()->GetAttributeList().GetAttributeByName("paintkit_proto_def_index");
			if (attr != nullptr) {
				attr->GetValuePtr()->m_UInt = attr->GetValuePtr()->m_Float;
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CBaseCombatWeapon_Equip, CBaseCombatCharacter *owner)
	{
		DETOUR_MEMBER_CALL(CBaseCombatWeapon_Equip)(owner);
		auto ent = reinterpret_cast<CBaseCombatWeapon *>(this);
		
		GET_STRING_ATTRIBUTE(ent->GetItem()->GetAttributeList(), "attachment name", attachmentname);
		if (attachmentname != nullptr) {
			ent->SetEffects(ent->GetEffects() & ~(EF_BONEMERGE));
		}
		THINK_FUNC_SET(ent, WarpaintAttributeCorrection, gpGlobals->curtime);
	}
	
	float bounce_damage_bonus = 0.0f;

	void ExplosionCustomSet(CBaseProjectile *proj)
	{
		auto launcher = static_cast<CTFWeaponBase *>(ToBaseCombatWeapon(proj->GetOriginalLauncher()));
		if (launcher != nullptr) {
			GetExplosionParticle(launcher->GetTFPlayerOwner(), launcher,particle_to_use);
			GET_STRING_ATTRIBUTE(launcher->GetItem()->GetAttributeList(), "custom impact sound", sound);
			
			if (sound != nullptr) {
				PrecacheSound(sound);
				proj->EmitSound(sound);
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CTFWeaponBaseGrenadeProj_Explode, trace_t *pTrace, int bitsDamageType)
	{
		particle_to_use = 0;
		auto proj = reinterpret_cast<CTFWeaponBaseGrenadeProj *>(this);
		
		if (bounce_damage_bonus != 0.0f) {
			proj->SetDamage(proj->GetDamage() * (1 + bounce_damage_bonus));
		}

		ExplosionCustomSet(proj);
		DETOUR_MEMBER_CALL(CTFWeaponBaseGrenadeProj_Explode)(pTrace, bitsDamageType);
		particle_to_use = 0;
	}

	DETOUR_DECL_MEMBER(void, CTFBaseRocket_Explode, trace_t *pTrace, CBaseEntity *pOther)
	{
		particle_to_use = 0;
		auto proj = reinterpret_cast<CTFBaseRocket *>(this);
		ExplosionCustomSet(proj);
		DETOUR_MEMBER_CALL(CTFBaseRocket_Explode)(pTrace, pOther);

		particle_to_use = 0;
	}
	
	DETOUR_DECL_MEMBER(void, CTFProjectile_EnergyBall_Explode, trace_t *pTrace, CBaseEntity *pOther)
	{
		auto proj = reinterpret_cast<CTFProjectile_EnergyBall *>(this);
		ExplosionCustomSet(proj);
		DETOUR_MEMBER_CALL(CTFProjectile_EnergyBall_Explode)(pTrace, pOther);
	}

	DETOUR_DECL_STATIC(void, TE_TFExplosion, IRecipientFilter &filter, float flDelay, const Vector &vecOrigin, const Vector &vecNormal, int iWeaponID, int nEntIndex, int nDefID, int nSound, int iCustomParticle)
	{
		//CBasePlayer *playerbase;
		//if (nEntIndex > 0 && (playerbase = UTIL_PlayerByIndex(nEntIndex)) != nullptr) {
		if (particle_to_use != 0)
			iCustomParticle = particle_to_use;

		if (particle_to_use == -1) return;

		DETOUR_STATIC_CALL(TE_TFExplosion)(filter, flDelay, vecOrigin, vecNormal, iWeaponID, nEntIndex, nDefID, nSound, iCustomParticle);
	}

	const char *weapon_sound_override = nullptr;
	CBasePlayer *weapon_sound_override_owner = nullptr;
	DETOUR_DECL_MEMBER(void, CBaseCombatWeapon_WeaponSound, int index, float soundtime) 
	{
		if ((index == 1 || index == 5 || index == 6 || index == 11 || index == 13)) {
			auto weapon = reinterpret_cast<CTFWeaponBase *>(this);

			int weaponid = weapon->GetWeaponID();
			// Allow SPECIAL1 sound for Cow Mangler and SPECIAL3 sound for short circuit
			if (index == 11 && weaponid != TF_WEAPON_PARTICLE_CANNON) return;
			if (index == 13 && weaponid != TF_WEAPON_MECHANICAL_ARM) return;

			int attr_name = -1;

			static int custom_weapon_fire_sound = GetItemSchema()->GetAttributeDefinitionByName("custom weapon fire sound")->GetIndex();
			static int custom_weapon_reload_sound = GetItemSchema()->GetAttributeDefinitionByName("custom weapon reload sound")->GetIndex();
			switch (index) {
				case 1: case 5: case 11: case 13: attr_name = custom_weapon_fire_sound; break;
				case 6: attr_name = custom_weapon_reload_sound; break;
			}
			
			const char *modelname = GetStringAttribute(weapon->GetItem()->GetAttributeList(), attr_name);
			if (weapon->GetOwner() != nullptr && modelname != nullptr) {
				
				PrecacheSound(modelname);
				weapon_sound_override_owner = ToTFPlayer(weapon->GetOwner());
				weapon_sound_override = modelname;

				weapon->GetOwner()->EmitSound(modelname, soundtime);
				return;
			}
		}
		DETOUR_MEMBER_CALL(CBaseCombatWeapon_WeaponSound)(index, soundtime);
		weapon_sound_override = nullptr;
	}

	DETOUR_DECL_STATIC(void, CBaseEntity_EmitSound, IRecipientFilter& filter, int iEntIndex, const char *sound, const Vector *pOrigin, float start, float *duration )
	{
		if (weapon_sound_override != nullptr) {
			//reinterpret_cast<CRecipientFilter &>(filter).AddRecipient(weapon_sound_override_owner);
			sound = weapon_sound_override;
		}
		DETOUR_STATIC_CALL(CBaseEntity_EmitSound)(filter, iEntIndex, sound, pOrigin, start, duration);
	}

	int fire_bullet_num_shot = 0;
	RefCount rc_CTFPlayer_FireBullet;
	DETOUR_DECL_MEMBER(void, CTFPlayer_FireBullet, CTFWeaponBase *weapon, FireBulletsInfo_t& info, bool bDoEffects, int nDamageType, int nCustomDamageType)
	{
		SCOPED_INCREMENT(rc_CTFPlayer_FireBullet);
		if (fire_bullet_num_shot == 0) {
			float rangeLimit = 0.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, rangeLimit, max_bullet_range);
			if (rangeLimit != 0.0f) {
				info.m_flDistance = rangeLimit;
			}
		}
		DETOUR_MEMBER_CALL(CTFPlayer_FireBullet)(weapon, info, bDoEffects, nDamageType, nCustomDamageType);
		fire_bullet_num_shot++;
	}

	DETOUR_DECL_STATIC(void, FX_FireBullets, CTFWeaponBase *pWpn, int iPlayer, const Vector &vecOrigin, const QAngle &vecAngles,
					 int iWeapon, int iMode, int iSeed, float flSpread, float flDamage, bool bCritical)
	{
		fire_bullet_num_shot = 0;
		DETOUR_STATIC_CALL(FX_FireBullets)(pWpn, iPlayer, vecOrigin, vecAngles, iWeapon, iMode, iSeed, flSpread, flDamage, bCritical);
		fire_bullet_num_shot = 0;
	}
	
	DETOUR_DECL_MEMBER(bool, CTFProjectile_Arrow_StrikeTarget, mstudiobbox_t *bbox, CBaseEntity *ent)
	{
		int can_headshot = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(reinterpret_cast<CTFProjectile_Arrow *>(this)->GetOriginalLauncher(), can_headshot, cannot_be_headshot);
		if (can_headshot != 0 && bbox->group == HITGROUP_HEAD) {
			bbox->group = HITGROUP_CHEST;
		}
		return DETOUR_MEMBER_CALL(CTFProjectile_Arrow_StrikeTarget)(bbox, ent);
	}

	RefCount rc_CBaseEntity_DispatchTraceAttack;
	DETOUR_DECL_MEMBER(void, CBaseEntity_DispatchTraceAttack, const CTakeDamageInfo& info, const Vector& vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator)
	{
		
		auto ent = reinterpret_cast<CBaseEntity *>(this);
		SCOPED_INCREMENT(rc_CBaseEntity_DispatchTraceAttack);
		bool useinfomod = false;
		CTakeDamageInfo infomod = info;
		if ((info.GetDamageType() & DMG_USE_HITLOCATIONS) && info.GetWeapon() != nullptr) {
			int can_headshot = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(ent, can_headshot, cannot_be_headshot);
			if (can_headshot != 0) {
				infomod.SetDamageType(info.GetDamageType() & ~DMG_USE_HITLOCATIONS);
				useinfomod = true;
			}
		}
		//int predamagecustom = info.GetDamageCustom();
		//int predamage = info.GetDamageType();
		
		if (rc_CTFPlayer_FireBullet > 0 && ptr != nullptr && rc_CTFGameRules_RadiusDamage == 0 && (ptr->surface.flags & SURF_SKY) == 0) {
			auto weapon = static_cast<CTFWeaponBase *>(ToBaseCombatWeapon(info.GetWeapon()));
			if (weapon != nullptr) {
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "custom impact sound", sound);
				if (sound != nullptr) {
					PrecacheSound(sound);
					CRecipientFilter filter;
					filter.AddRecipientsByPAS(ptr->endpos);
					EmitSound_t params;
                    params.m_pSoundName = sound;
                    params.m_flSoundTime = 0.0f;
                    params.m_pflSoundDuration = nullptr;
                    params.m_bWarnOnDirectWaveReference = true;
					params.m_pOrigin = &ptr->endpos;
					params.m_nChannel = CHAN_WEAPON;
                    CBaseEntity::EmitSound(filter, ENTINDEX(ptr->DidHit() ? ptr->m_pEnt : ent), params);

					//CBaseEntity::EmitSound(filter, 0, sound, &ptr->endpos);
				}

				int attr_explode_bullet = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, attr_explode_bullet, explosive_bullets);
				if (weapon && attr_explode_bullet != 0) {
					
					CRecipientFilter filter;
					filter.AddRecipientsByPVS(ptr->endpos);

					int iLargeExplosion = 0;
					CALL_ATTRIB_HOOK_INT_ON_OTHER( weapon, iLargeExplosion, use_large_smoke_explosion );
					if ( iLargeExplosion > 0 )
					{
						force_send_client = true;
						DispatchParticleEffect( "explosionTrail_seeds_mvm", ptr->endpos , vec3_angle );
						DispatchParticleEffect( "fluidSmokeExpl_ring_mvm", ptr->endpos , vec3_angle);
						force_send_client = false;
					}
					int customparticle = INVALID_STRING_INDEX;
					GetExplosionParticle(ToTFPlayer(weapon->GetOwnerEntity()), weapon, customparticle);

					if (customparticle != -1)
						TE_TFExplosion( filter, 0.0f, ptr->endpos, Vector(0,0,1), TF_WEAPON_GRENADELAUNCHER, ENTINDEX(info.GetAttacker()), -1, 11/*SPECIAL1*/, customparticle );

					CTFRadiusDamageInfo radiusinfo;
					CTakeDamageInfo info2( weapon, weapon->GetOwnerEntity(), weapon, vec3_origin, ptr->endpos, info.GetDamage(), (infomod.GetDamageType() | DMG_BLAST) & (~DMG_BULLET), infomod.GetDamageCustom() );

					radiusinfo.m_DmgInfo = &info2;
					radiusinfo.m_vecOrigin = ptr->endpos;
					radiusinfo.m_flRadius = attr_explode_bullet;
					radiusinfo.m_pEntityIgnore = ent;
					radiusinfo.m_unknown_18 = 0.0f;
					radiusinfo.m_unknown_1c = 1.0f;
					radiusinfo.target = nullptr;
					radiusinfo.m_flFalloff = 0.5f;

					Vector origpos = weapon->GetAbsOrigin();
					weapon->SetAbsOrigin(ptr->endpos - (weapon->WorldSpaceCenter() - origpos));
					TFGameRules()->RadiusDamage( radiusinfo );
					weapon->SetAbsOrigin(origpos);
					DETOUR_MEMBER_CALL(CBaseEntity_DispatchTraceAttack)(info2, vecDir, ptr, pAccumulator);
					return;
				}
			}
		}
		if (stickbomb != nullptr) {
			if (rc_CTFGameRules_RadiusDamage == 0) {
				
				minicrit = (info.GetDamageType() & DMG_RADIUS_MAX) != 0;
				stickbomb->GetTFPlayerOwner()->m_Shared->AddCond(TF_COND_NOHEALINGDAMAGEBUFF, 99.0f);
				DETOUR_MEMBER_CALL(CBaseEntity_DispatchTraceAttack)(info, vecDir, ptr, pAccumulator);
				stickbomb->GetTFPlayerOwner()->m_Shared->RemoveCond(TF_COND_NOHEALINGDAMAGEBUFF);
				DevMsg("mini crit set: %d\n",minicrit);
				return;
			}
		}

		DETOUR_MEMBER_CALL(CBaseEntity_DispatchTraceAttack)(useinfomod ? infomod : info, vecDir, ptr, pAccumulator);
	}

	DETOUR_DECL_MEMBER(bool, CRecipientFilter_IgnorePredictionCull)
	{
		return force_send_client || DETOUR_MEMBER_CALL(CRecipientFilter_IgnorePredictionCull)();
	}

	DETOUR_DECL_MEMBER(float, CTFCompoundBow_GetProjectileSpeed)
	{
		auto bow = reinterpret_cast<CTFCompoundBow *>(this);
		float speed = 1.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(bow, speed, mult_projectile_speed);
		speed *= DETOUR_MEMBER_CALL(CTFCompoundBow_GetProjectileSpeed)();
		return speed;
	}

	DETOUR_DECL_MEMBER(float, CTFProjectile_EnergyRing_GetInitialVelocity)
	{
		auto proj = reinterpret_cast<CTFProjectile_EnergyRing *>(this);
		float speed = 1.0f;
		if (proj->GetOriginalLauncher() != nullptr) {
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(proj->GetOriginalLauncher(), speed, mult_projectile_speed);
		}
		return speed * DETOUR_MEMBER_CALL(CTFProjectile_EnergyRing_GetInitialVelocity)();
	}

	DETOUR_DECL_MEMBER(CBaseEntity *, CTFWeaponBaseGun_FireEnergyBall, CTFPlayer *player, bool ring)
	{
		auto proj = DETOUR_MEMBER_CALL(CTFWeaponBaseGun_FireEnergyBall)(player, ring);
		if (ring && proj != nullptr) {
			auto weapon = reinterpret_cast<CTFWeaponBaseGun *>(this);
			Vector vel = proj->GetAbsVelocity();
			float speed = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, speed, mult_projectile_speed);
			proj->SetAbsVelocity(vel * speed);
		}
		return proj;
	}

	DETOUR_DECL_MEMBER(float, CTFCrossbow_GetProjectileSpeed)
	{
		auto bow = reinterpret_cast<CTFWeaponBase *>(this);
		float speed = 1.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(bow, speed, mult_projectile_speed);
		return speed * DETOUR_MEMBER_CALL(CTFCrossbow_GetProjectileSpeed)();
	}

	DETOUR_DECL_MEMBER(float, CTFGrapplingHook_GetProjectileSpeed)
	{
		auto bow = reinterpret_cast<CTFWeaponBase *>(this);
		float speed = 1.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(bow, speed, mult_projectile_speed);
		return speed * DETOUR_MEMBER_CALL(CTFGrapplingHook_GetProjectileSpeed)();
	}

	DETOUR_DECL_MEMBER(float, CTFShotgunBuildingRescue_GetProjectileSpeed)
	{
		auto bow = reinterpret_cast<CTFWeaponBase *>(this);
		float speed = 1.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(bow, speed, mult_projectile_speed);
		return speed * DETOUR_MEMBER_CALL(CTFShotgunBuildingRescue_GetProjectileSpeed)();
	}

	DETOUR_DECL_MEMBER(int, CTFWeaponBase_GetDamageType)
	{
		int value = DETOUR_MEMBER_CALL(CTFWeaponBase_GetDamageType)();

		auto weapon = reinterpret_cast<CTFWeaponBase *>(this);
		if ((value & DMG_USE_HITLOCATIONS) == 0) {
			int headshot = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, headshot, can_headshot);
			if (headshot) {
				value |= DMG_USE_HITLOCATIONS;
			}
		}
		else
		{
			int noheadshot = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, noheadshot, cannot_headshot);
			if (noheadshot) {
				value &= ~DMG_USE_HITLOCATIONS;
			}
		}

		if ((value & DMG_USEDISTANCEMOD) != 0) {
			int nofalloff= 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, nofalloff, no_damage_falloff);
			if (nofalloff) {
				value &= ~DMG_USEDISTANCEMOD;
			}
		}
		else
		{ 
			int falloff= 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, falloff, force_damage_falloff);
			if (falloff) {
				value |= DMG_USEDISTANCEMOD;
			}
		}

		if ((value & DMG_NOCLOSEDISTANCEMOD) != 0) {
			int noclosedistancemod= 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, noclosedistancemod, no_reduced_damage_rampup);
			if (noclosedistancemod) {
				value &= ~DMG_NOCLOSEDISTANCEMOD;
			}
		}
		else
		{ 
			int closedistancemod= 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, closedistancemod, reduced_damage_rampup);
			if (closedistancemod) {
				value |= DMG_NOCLOSEDISTANCEMOD;
			}
		}

		if ((value & DMG_DONT_COUNT_DAMAGE_TOWARDS_CRIT_RATE) == 0) {
			int crit_rate_damage = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, crit_rate_damage, dont_count_damage_towards_crit_rate);
			if (crit_rate_damage) {
				value |= DMG_DONT_COUNT_DAMAGE_TOWARDS_CRIT_RATE;
			}
		}

		int iAddDamageType = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, iAddDamageType, add_damage_type);
		value |= iAddDamageType;

		int iRemoveDamageType = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, iRemoveDamageType, remove_damage_type);
		value &= ~iRemoveDamageType;

		/*if (info.GetWeapon() != nullptr) {
			auto weapon = rtti_cast<CTFWeaponBase *>(info.GetWeapon());
			if (weapon != nullptr) {
				int headshot = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, headshot, can_headshot);
				if (headshot) {
					CTakeDamageInfo info2 = info;
					info2.SetDamageType(can_headshot);
				}
			}
		}*/
		return value;
	}

	bool CanHeadshot(CBaseProjectile *projectile) {
		CBaseEntity *shooter = projectile->GetOriginalLauncher();

		if (shooter != nullptr) {
			int headshot = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(shooter, headshot, can_headshot);
			DevMsg("can headshot %d",headshot);
			return headshot != 0;
		}
		return false;
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_HealingBolt_CanHeadshot)
	{
		return CanHeadshot(reinterpret_cast<CBaseProjectile *>(this)) || DETOUR_MEMBER_CALL(CTFProjectile_HealingBolt_CanHeadshot)();
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_EnergyRing_CanHeadshot)
	{
		return CanHeadshot(reinterpret_cast<CBaseProjectile *>(this)) || DETOUR_MEMBER_CALL(CTFProjectile_EnergyRing_CanHeadshot)();
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_GrapplingHook_CanHeadshot)
	{
		return CanHeadshot(reinterpret_cast<CBaseProjectile *>(this)) || DETOUR_MEMBER_CALL(CTFProjectile_GrapplingHook_CanHeadshot)();
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_EnergyBall_CanHeadshot)
	{
		return CanHeadshot(reinterpret_cast<CBaseProjectile *>(this)) || DETOUR_MEMBER_CALL(CTFProjectile_EnergyBall_CanHeadshot)();
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_Arrow_CanHeadshot)
	{
		auto projectile = reinterpret_cast<CBaseProjectile *>(this);
		if (CanHeadshot(projectile))
			return true;

		CBaseEntity *shooter = projectile->GetOriginalLauncher();

		if (shooter != nullptr) {
			int headshot = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(shooter, headshot, cannot_headshot);
			if (headshot == 1)
				return false;
		}

		return DETOUR_MEMBER_CALL(CTFProjectile_Arrow_CanHeadshot)();
	}

	DETOUR_DECL_MEMBER(int, CBaseCombatCharacter_OnTakeDamage, const CTakeDamageInfo& info)
	{
		if (info.GetWeapon() != nullptr) {
			auto character = reinterpret_cast<CBaseCombatCharacter *>(this);
			if (character->MyNextBotPointer() != nullptr && !character->IsPlayer() && !character->ClassMatches("tank_boss")) {
				float dmg_mult = 1.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), dmg_mult, mult_dmg_vs_npc);
				if (dmg_mult != 1.0f) {
					CTakeDamageInfo newinfo = info;
					newinfo.SetDamage(newinfo.GetDamage() * dmg_mult);
					return DETOUR_MEMBER_CALL(CBaseCombatCharacter_OnTakeDamage)(newinfo);
				}
			}
		}
		return DETOUR_MEMBER_CALL(CBaseCombatCharacter_OnTakeDamage)(info);
	}

	DETOUR_DECL_MEMBER(int, CTFGameRules_ApplyOnDamageModifyRules, CTakeDamageInfo& info, CBaseEntity *pVictim, bool b1)
	{
		
		//Allow halloween kart to do more damage based on attributes

		if (info.GetAttacker() != nullptr && info.GetAttacker()->IsPlayer() && info.GetDamageCustom() == TF_DMG_CUSTOM_KART)
		{
			float dmg = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER ( info.GetAttacker(), dmg, mult_dmg );
			if (pVictim->IsPlayer())
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER ( info.GetAttacker(), dmg, mult_dmg_vs_players );

			info.SetDamage(info.GetDamage() * dmg);
		}

		//Allow mantreads to do more damage based on attributes

		if (info.GetAttacker() != nullptr && info.GetAttacker()->IsPlayer() && info.GetDamageCustom() == TF_DMG_CUSTOM_BOOTS_STOMP && info.GetWeapon() != nullptr && info.GetWeapon()->IsWearable())
		{
			float dmg = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER ( info.GetWeapon(), dmg, mult_dmg );
			if (pVictim->IsPlayer())
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER ( info.GetWeapon(), dmg, mult_dmg_vs_players );

			info.SetDamage(info.GetDamage() * dmg);
		}

		if (info.GetWeapon() != nullptr && info.GetWeapon()->MyCombatWeaponPointer() != nullptr) {
			float dmg = info.GetDamage();


			// Allow mult_dmg_bonus_while_half_dead mult_dmg_penalty_while_half_alive mult_dmg_with_reduced_health
			// to work on non melee weapons
			//
			if (info.GetAttacker() != nullptr && !info.GetWeapon()->MyCombatWeaponPointer()->IsMeleeWeapon()) {
				float maxHealth = info.GetAttacker()->GetMaxHealth();
				float health = info.GetAttacker()->GetHealth();
				float halfHealth = maxHealth * 0.5f;
				float dmgmult = 1.0f;
				if ( health < halfHealth )
				{
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetAttacker(), dmgmult, mult_dmg_bonus_while_half_dead );
				}
				else
				{
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetAttacker(), dmgmult, mult_dmg_penalty_while_half_alive );
				}

				// Some weapons change damage based on player's health
				float flReducedHealthBonus = 1.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetAttacker(), flReducedHealthBonus, mult_dmg_with_reduced_health );
				if ( flReducedHealthBonus != 1.0f )
				{
					float flHealthFraction = clamp( health / maxHealth, 0.0f, 1.0f );
					flReducedHealthBonus = Lerp( flHealthFraction, flReducedHealthBonus, 1.0f );

					dmgmult *= flReducedHealthBonus;
				}
				dmg *= dmgmult;
			}

			float iDmgCurrentHealth = 0.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), iDmgCurrentHealth, dmg_current_health);
			if (iDmgCurrentHealth != 0.0f)
			{
				dmg += pVictim->GetHealth() * iDmgCurrentHealth;
			}

			float iDmgMaxHealth = 0.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), iDmgMaxHealth, dmg_max_health);
			if (iDmgMaxHealth != 0.0f)
			{
				dmg += pVictim->GetMaxHealth() * iDmgMaxHealth;
			}

			float iDmgMissingHealth = 0.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), iDmgMissingHealth, dmg_missing_health);
			if (iDmgMissingHealth != 0.0f)
			{
				dmg += (pVictim->GetMaxHealth() - pVictim->GetHealth()) * iDmgMissingHealth;
			}

			auto playerVictim = ToTFPlayer(pVictim);
			if (playerVictim != nullptr) {
				int iDmgType = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(info.GetWeapon(), iDmgType, special_damage_type);
				float dmg_mult = 1.0f;
				if (iDmgType == 1) {
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(pVictim, dmg_mult, dmg_taken_mult_from_special_damage_type_1);
				}
				else if (iDmgType == 2) {
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(pVictim, dmg_mult, dmg_taken_mult_from_special_damage_type_2);
				}
				else if (iDmgType == 3) {
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(pVictim, dmg_mult, dmg_taken_mult_from_special_damage_type_3);
				}
				if (playerVictim->IsMiniBoss()) {
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), dmg_mult, mult_dmg_vs_giants);
				}

				if (playerVictim->InAirDueToKnockback()) {
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER ( info.GetWeapon(), dmg_mult, mult_dmg_vs_airborne );
				}

				dmg *= dmg_mult;
			}
			else if (pVictim->MyNextBotPointer() != nullptr) {
				if (pVictim->ClassMatches("tank_boss")) {
					float dmg_mult = 1.0f;
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), dmg_mult, mult_dmg_vs_tanks);
					dmg *= dmg_mult;
				}
			}
			info.SetDamage(dmg);
		}

		int ret = DETOUR_MEMBER_CALL(CTFGameRules_ApplyOnDamageModifyRules)(info, pVictim, b1);
		if ((info.GetDamageType() & DMG_CRITICAL) && info.GetWeapon() != nullptr) {
			float crit = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), crit, mult_crit_dmg);
			float dmg = info.GetDamage();
			info.SetDamageBonus(dmg * crit - (dmg - info.GetDamageBonus()));
			info.SetDamage(dmg * crit);
		}
		return ret;
	}

	//Allow can holster while spinning to work with firing minigun
	DETOUR_DECL_MEMBER(bool, CTFMinigun_CanHolster) {
		auto minigun = reinterpret_cast<CTFMinigun *>(this);
		bool firing = minigun->m_iWeaponState == CTFMinigun::AC_STATE_FIRING;
		if (firing)
			minigun->m_iWeaponState = CTFMinigun::AC_STATE_SPINNING;
		bool ret = DETOUR_MEMBER_CALL(CTFMinigun_CanHolster)();
		if (firing)
			minigun->m_iWeaponState = CTFMinigun::AC_STATE_FIRING;
			
		return ret;
	}

	DETOUR_DECL_MEMBER(void, CObjectSapper_ApplyRoboSapperEffects, CTFPlayer *target, float duration) {
		int cannotApply = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(target, cannotApply, cannot_be_sapped);
		if (!cannotApply)
			DETOUR_MEMBER_CALL(CObjectSapper_ApplyRoboSapperEffects)(target, duration);
	}

	DETOUR_DECL_MEMBER(bool, CObjectSapper_IsParentValid) {
		bool ret = DETOUR_MEMBER_CALL(CObjectSapper_IsParentValid)();
		if (ret) {
			CTFPlayer * player = ToTFPlayer(reinterpret_cast<CBaseObject *>(this)->GetBuiltOnEntity());
			if (player != nullptr) {
				int cannotApply = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(player, cannotApply, cannot_be_sapped);
				if (cannotApply)
					ret = false;
			}
		}

		return ret;
	}

	DETOUR_DECL_MEMBER(bool, CTFPlayer_IsAllowedToTaunt) {
		bool ret = DETOUR_MEMBER_CALL(CTFPlayer_IsAllowedToTaunt)();
		auto player = reinterpret_cast<CTFPlayer *>(this);

		if (ret) {

			int cannotTaunt = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(player, cannotTaunt, cannot_taunt);
			if (cannotTaunt)
				ret = false;
		}
		else if (player->IsAlive()) {

			int allowTaunt = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(player, allowTaunt, always_allow_taunt);

			if (allowTaunt > 1 || (allowTaunt == 1 && !player->m_Shared->InCond(TF_COND_TAUNTING)))
				ret = true;
		}

		return ret;
	}

	DETOUR_DECL_MEMBER(void, CUpgrades_UpgradeTouch, CBaseEntity *pOther)
	{
		if (TFGameRules()->IsMannVsMachineMode()) {
			CTFPlayer *player = ToTFPlayer(pOther);
			if (player != nullptr) {
				int cannotUpgrade = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(player,cannotUpgrade,cannot_upgrade);
				if (cannotUpgrade) {
					gamehelpers->TextMsg(ENTINDEX(player), TEXTMSG_DEST_CENTER, "The Upgrade Station is disabled!");
                    if (Mod::Pop::PopMgr_Extensions::HasExtraLoadoutItems(player->GetPlayerClass()->GetClassIndex())) {
                        Mod::Pop::PopMgr_Extensions::DisplayExtraLoadoutItemsClass(player, player->GetPlayerClass()->GetClassIndex());
					}
					return;
				}
			}
		}
		
		DETOUR_MEMBER_CALL(CUpgrades_UpgradeTouch)(pOther);
	}

	DETOUR_DECL_MEMBER(void, CUpgrades_EndTouch, CBaseEntity *pOther)
	{
		if (TFGameRules()->IsMannVsMachineMode()) {
			CTFPlayer *player = ToTFPlayer(pOther);
			if (player != nullptr) {
				int cannotUpgrade = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(player,cannotUpgrade,cannot_upgrade);
				if (cannotUpgrade) {
					gamehelpers->TextMsg(ENTINDEX(player), TEXTMSG_DEST_CENTER, "The Upgrade Station is disabled!");
					if (Mod::Pop::PopMgr_Extensions::HasExtraLoadoutItems(player->GetPlayerClass()->GetClassIndex())) {
        				menus->GetDefaultStyle()->CancelClientMenu(ENTINDEX(player));
					}
				}
			}
		}
		
		DETOUR_MEMBER_CALL(CUpgrades_EndTouch)(pOther);
	}

	class WeaponModule : public EntityModule
    {
    public:
	WeaponModule(CBaseEntity *entity) : EntityModule(entity) {}

	float crouchAccuracy = 1.0f;
	bool crouchAccuracyApplied = false;
	bool lastMoveAccuracyApplied = 1.0f;
	float consecutiveShotsScore = 0.0f;
	float lastConsecutiveShotsApplied = 1.0f;
	float totalAccuracyApplied = 1.0f;

	float lastShotTime = 0.0f;
	int burstShotNumber = 0;
	float lastAttackCooldown = 0.0f;
	float attackTimeSave = 0.0f;
    };

	void OnWeaponUpdate(CTFWeaponBase *weapon) {
		CTFPlayer *owner = ToTFPlayer(weapon->GetOwnerEntity());
		if (owner != nullptr && (gpGlobals->tickcount % 5) == 3) {
			int alwaysCrit = GetFastAttributeInt(weapon, 0, ALWAYS_CRIT);

			if (alwaysCrit) {
				owner->m_Shared->AddCond(TF_COND_CRITBOOSTED_CARD_EFFECT, 0.5f, nullptr);
			}
			
			int addcond = GetFastAttributeInt(weapon, 0, ADD_COND_ON_ACTIVE);
			if (addcond != 0) {
				for (int i = 0; i < 3; i++) {
					int addcond_single = (addcond >> (i * 8)) & 255;
					if (addcond_single != 0) {
						owner->m_Shared->AddCond((ETFCond)addcond_single, 0.5f, owner);
					}
				}
			}
			float crouch_accuracy = GetFastAttributeFloat(weapon, 1.0f, DUCK_ACCURACY_MULT);
			float consecutive_accuracy = GetFastAttributeFloat(weapon, 1.0f, CONTINOUS_ACCURACY_MULT);
			float move_accuracy = GetFastAttributeFloat(weapon, 1.0f, MOVE_ACCURACY_MULT);

			if (crouch_accuracy != 1.0f || consecutive_accuracy != 1.0f) {
				auto mod = weapon->GetOrCreateEntityModule<WeaponModule>("weapon");
				static int accuracy_penalty_id = GetItemSchema()->GetAttributeDefinitionByName("spread penalty")->GetIndex();
				float applyAccuracy = 1.0f;
				bool doApplyAccuracy = false;
				//auto attr = weapon->GetItem()->GetAttributeList().GetAttributeByID(accuracy_penalty_id);

				if (crouch_accuracy != 1.0f) {
					mod->crouchAccuracyApplied = owner->m_Local->m_bDucked;
					if (mod->crouchAccuracyApplied) {
						applyAccuracy *= crouch_accuracy;
					}
				}

				if (consecutive_accuracy != 1.0f) {
					float consecutive_accuracy_time = GetFastAttributeFloat(weapon, 0.0f, CONTINOUS_ACCURACY_TIME);
					float consecutiveAccuracyApply = RemapValClamped(mod->consecutiveShotsScore, 0.0f, consecutive_accuracy_time, 1.0f, consecutive_accuracy);
					applyAccuracy *= consecutiveAccuracyApply;
					if (weapon->m_flNextPrimaryAttack <= gpGlobals->curtime || weapon->m_bInReload) {
						mod->consecutiveShotsScore = Clamp(mod->consecutiveShotsScore - gpGlobals->frametime * 5 * 5, 0.0f, consecutive_accuracy_time);
					}
					mod->lastConsecutiveShotsApplied = consecutiveAccuracyApply;
				}

				if (move_accuracy != 1.0f) {
					float consecutiveAccuracyApply = RemapValClamped(owner->GetAbsVelocity().Length(), 0.0f, owner->TeamFortress_CalculateMaxSpeed() * 0.5f, 1.0f, move_accuracy);
					applyAccuracy *= consecutiveAccuracyApply;
					mod->lastMoveAccuracyApplied = consecutiveAccuracyApply;
				}
				doApplyAccuracy = mod->totalAccuracyApplied != applyAccuracy;
				if (doApplyAccuracy) {
					if (applyAccuracy == 1.0f) {
						weapon->GetItem()->GetAttributeList().RemoveAttributeByDefID(accuracy_penalty_id);
					}
					else {
						weapon->GetItem()->GetAttributeList().SetRuntimeAttributeValueByDefID(accuracy_penalty_id, applyAccuracy);
					}
				}
				mod->totalAccuracyApplied = applyAccuracy;
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CTFWeaponBase_ItemPostFrame)
	{
		auto weapon = reinterpret_cast<CTFWeaponBase *>(this);
		OnWeaponUpdate(weapon);
		
		DETOUR_MEMBER_CALL(CTFWeaponBase_ItemPostFrame)();
	}
	DETOUR_DECL_MEMBER(void, CTFWeaponBase_ItemBusyFrame)
	{
		auto weapon = reinterpret_cast<CTFWeaponBase *>(this);
		OnWeaponUpdate(weapon);

		DETOUR_MEMBER_CALL(CTFWeaponBase_ItemBusyFrame)();
	}

	DETOUR_DECL_MEMBER(bool, CObjectSentrygun_FireRocket)
	{
		bool ret = DETOUR_MEMBER_CALL(CObjectSentrygun_FireRocket)();
		if (ret) {
			auto sentrygun = reinterpret_cast<CObjectSentrygun *>(this);
			CTFPlayer *owner = sentrygun->GetBuilder();
			if (owner != nullptr) {
				float fireRocketRate = 1.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(owner,fireRocketRate,mult_firerocket_rate);
				if (fireRocketRate != 1.0f) {
					sentrygun->m_flNextRocketFire = (sentrygun->m_flNextRocketFire - gpGlobals->curtime) * fireRocketRate + gpGlobals->curtime;
				}
			}
		}
		return ret;
	}

	DETOUR_DECL_MEMBER(bool, CBaseObject_CanBeUpgraded, CTFPlayer *player)
	{
		bool ret = DETOUR_MEMBER_CALL(CBaseObject_CanBeUpgraded)(player);
		if (ret) {
			auto obj = reinterpret_cast<CBaseObject *>(this);
			CTFPlayer *owner = obj->GetBuilder();
			if (owner != nullptr) {
				int maxLevel = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(owner,maxLevel,building_max_level);
				if (maxLevel != 0) {
					return obj->m_iUpgradeLevel < maxLevel;
				}
			}
		}
		return ret;
	}

	DETOUR_DECL_MEMBER(void, CBaseObject_StartUpgrading)
	{
		auto obj = reinterpret_cast<CBaseObject *>(this);
		CTFPlayer *owner = obj->GetBuilder();
		if (owner != nullptr) {
			int maxLevel = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(owner,maxLevel,building_max_level);
			if (maxLevel > 0 && obj->m_iUpgradeLevel >= maxLevel) {
				servertools->SetKeyValue(obj, "defaultupgrade", CFmtStr("%d", maxLevel - 1));
				return;
			}
		}
		DETOUR_MEMBER_CALL(CBaseObject_StartUpgrading)();
	}

	DETOUR_DECL_MEMBER(void, CObjectSentrygun_SentryThink)
	{
		auto obj = reinterpret_cast<CObjectSentrygun *>(this);
		CTFPlayer *owner = obj->GetBuilder();
		DETOUR_MEMBER_CALL(CObjectSentrygun_SentryThink)();
		if (owner != nullptr) {
			int rapidTick = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(owner,rapidTick,sentry_rapid_fire);
			if (rapidTick > 0) {
				//static BASEPTR addr = reinterpret_cast<BASEPTR> (AddrManager::GetAddr("CObjectSentrygun::SentryThink"));
				obj->ThinkSet(&CObjectSentrygun::SentryThink, gpGlobals->curtime, "SentrygunContext");
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CBaseObject_StartBuilding, CBaseEntity *builder)
	{
		DETOUR_MEMBER_CALL(CBaseObject_StartBuilding)(builder);
		auto obj = reinterpret_cast<CBaseObject *>(this);
		CTFPlayer *owner = obj->GetBuilder();
		if (owner != nullptr) {
			int color = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( owner, color, building_color_rgb);
			if (color != 0) {
				obj->SetRenderColorR(color >> 16);
				obj->SetRenderColorG((color >> 8) & 255);
				obj->SetRenderColorB(color & 255);
			}
			float scale = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( owner, scale, building_scale);
			if (scale != 1.0f) {
				obj->SetModelScale(scale);
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CBaseObject_StartPlacement, CTFPlayer *builder)
	{
		DETOUR_MEMBER_CALL(CBaseObject_StartPlacement)(builder);
		auto obj = reinterpret_cast<CBaseObject *>(this);
		if (builder != nullptr) {
			
			int color = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( builder, color, building_color_rgb);
			if (color != 0) {
				obj->SetRenderColorR(color >> 16);
				obj->SetRenderColorG((color >> 8) & 255);
				obj->SetRenderColorB(color & 255);
			}
			float scale = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( builder, scale, building_scale);
			if (scale != 1.0f) {
				obj->SetModelScale(scale);
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CObjectTeleporter_RecieveTeleportingPlayer, CTFPlayer *player)
	{
		auto tele = reinterpret_cast<CObjectTeleporter *>(this);
		if ( player == nullptr || tele->IsMarkedForDeletion() )
			return;

		Vector prepos = player->GetAbsOrigin();
		DETOUR_MEMBER_CALL(CObjectTeleporter_RecieveTeleportingPlayer)(player);
		Vector postpos = player->GetAbsOrigin();
		if (tele->GetModelScale() != 1.0f && prepos != postpos) {
			postpos.z = tele->WorldSpaceCenter().z + tele->CollisionProp()->OBBMaxs().z + 1.0f;
			player->Teleport(&postpos, &(player->GetAbsAngles()), &(player->GetAbsVelocity()));
		}
	}

	DETOUR_DECL_MEMBER(bool, CTFGameRules_FPlayerCanTakeDamage, CBasePlayer *pPlayer, CBaseEntity *pAttacker, const CTakeDamageInfo& info)
	{
		if (pPlayer != nullptr && pAttacker != nullptr && pPlayer->GetTeamNumber() == pAttacker->GetTeamNumber()) {
			int friendly = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( pAttacker, friendly, allow_friendly_fire);
			if (friendly != 0)
				return true;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( pPlayer, friendly, receive_friendly_fire);
			if (friendly != 0)
				return true;
		}
		
		return DETOUR_MEMBER_CALL(CTFGameRules_FPlayerCanTakeDamage)(pPlayer, pAttacker, info);
	}
	
	int friendy_fire_cache_tick = 0;
	std::unordered_map<CTFPlayer *, bool> friendy_fire_cache;

	DETOUR_DECL_MEMBER(bool, CTFPlayer_WantsLagCompensationOnEntity, const CBasePlayer *pPlayer, const CUserCmd *pCmd, const CBitVec<MAX_EDICTS> *pEntityTransmitBits)
	{
		auto player = reinterpret_cast<CTFPlayer *>(this);
		
		
		if ( gpGlobals->tickcount - friendy_fire_cache_tick > 7 || gpGlobals->tickcount < friendy_fire_cache_tick ) {
			friendy_fire_cache_tick = gpGlobals->tickcount;
			friendy_fire_cache.clear();
		}
		auto result = DETOUR_MEMBER_CALL(CTFPlayer_WantsLagCompensationOnEntity)(pPlayer, pCmd, pEntityTransmitBits);
		
		if (!result && player->GetTeamNumber() == pPlayer->GetTeamNumber()) {
			auto entry = friendy_fire_cache.find(player);

			if (entry == friendy_fire_cache.end()) {
				int friendly = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER( player, friendly, allow_friendly_fire);
				result = friendly != 0;
				friendy_fire_cache[player] = result;
			}
			else{
				result = entry->second;
			}
		}
		return result;
	}

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_StunPlayer, float duration, float slowdown, int flags, CTFPlayer *attacker)
	{
		auto shared = reinterpret_cast<CTFPlayerShared *>(this);
		
		auto player = shared->GetOuter();

		float stun = 1.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( player, stun, mult_stun_resistance);
		if (stun != 1.0f) {
			slowdown *= stun;
		}

		DETOUR_MEMBER_CALL(CTFPlayerShared_StunPlayer)(duration, slowdown, flags, attacker);
	}

	struct gamevcollisionevent_t : public vcollisionevent_t
	{
		Vector			preVelocity[2];
		Vector			postVelocity[2];
		AngularImpulse	preAngularVelocity[2];
		CBaseEntity		*pEntities[2];
	};

	CTFGrenadePipebombProjectile *grenade_proj;
	DETOUR_DECL_MEMBER(void, CTFGrenadePipebombProjectile_VPhysicsCollision, int index, gamevcollisionevent_t *pEvent)
	{
		grenade_proj = reinterpret_cast<CTFGrenadePipebombProjectile *>(this);

		int no_stick = 0;
		CBaseEntity *hit_ent = pEvent->pEntities[!index];
		CALL_ATTRIB_HOOK_INT_ON_OTHER(grenade_proj->GetOriginalLauncher(), no_stick, stickbomb_no_stick);
		if (no_stick != 0) {
			pEvent->pEntities[!index] = grenade_proj;
		}

		DETOUR_MEMBER_CALL(CTFGrenadePipebombProjectile_VPhysicsCollision)(index, pEvent);

		if (no_stick != 0) {
			pEvent->pEntities[!index] = hit_ent;
			float flFizzle = 0;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(grenade_proj->GetOriginalLauncher(), flFizzle, stickybomb_fizzle_time);
			if (flFizzle > 0) {
				grenade_proj->SetDetonateTimerLength(flFizzle);
			}
		}

		/*DevMsg("pre %d post %d touch\n", touched, grenade_proj->m_bTouched + 0);
		if (!touched && grenade_proj->m_bTouched) {
			float damage_bonus = 0.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(grenade_proj->GetOriginalLauncher(), damage_bonus, grenade_bounce_damage);
			DevMsg("Add bounce %f\n", grenade_proj->GetDamage() * damage_bonus);

			if (damage_bonus != 0.0f) {
				grenade_proj->SetDamage(grenade_proj->GetDamage() * damage_bonus);
			}
		}*/

		float bounce_speed = 0.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(grenade_proj->GetOriginalLauncher(), bounce_speed, grenade_bounce_speed);
		if (bounce_speed != 0.0f) {
			Vector &pre_vel = pEvent->preVelocity[index];
			Vector normal;
			pEvent->pInternalData->GetSurfaceNormal(normal);
			Vector mirror_vel = (pre_vel - 2 * (pre_vel.Dot(normal)) * normal) * bounce_speed;
			AngularImpulse angularVelocity;
			grenade_proj->VPhysicsGetObject()->GetVelocity( &normal, &angularVelocity );

			grenade_proj->VPhysicsGetObject()->SetVelocity( &mirror_vel, &angularVelocity );
		}

		grenade_proj = nullptr;
	}

	DETOUR_DECL_MEMBER(void, CTFGrenadePipebombProjectile_PipebombTouch, CBaseEntity *ent)
	{
		auto proj = reinterpret_cast<CTFGrenadePipebombProjectile *>(this);

		bounce_damage_bonus = 0.0f;
		if (proj->m_bTouched) {
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(proj->GetOriginalLauncher(), bounce_damage_bonus, grenade_bounce_damage);
			if (bounce_damage_bonus != 0.0f) {
				proj->m_bTouched = false;
			}
		}
		
		DETOUR_MEMBER_CALL(CTFGrenadePipebombProjectile_PipebombTouch)(ent);
	}

	DETOUR_DECL_STATIC(bool, PropDynamic_CollidesWithGrenades, CBaseEntity *ent)
	{
		if (grenade_proj != nullptr && grenade_proj->GetOriginalLauncher() != nullptr) {
			int explode_impact = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(grenade_proj->GetOriginalLauncher(), explode_impact, grenade_explode_on_impact);
			if (explode_impact != 0)
				return true;
		}

		return DETOUR_STATIC_CALL(PropDynamic_CollidesWithGrenades)(ent);
	}
	
	RefCount rc_CTFPlayer_RegenThink;
	DETOUR_DECL_MEMBER(void, CTFPlayer_RegenThink)
	{
		SCOPED_INCREMENT(rc_CTFPlayer_RegenThink);
		DETOUR_MEMBER_CALL(CTFPlayer_RegenThink)();
		
		CTFPlayer *player = reinterpret_cast<CTFPlayer *>(this);

		int iSuicideCounter = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER ( player, iSuicideCounter, is_suicide_counter );
		if (iSuicideCounter != 0) {
			Vector pos = player->GetAbsOrigin();
			CTakeDamageInfo info = CTakeDamageInfo(player, player, nullptr, vec3_origin, pos, iSuicideCounter, DMG_PREVENT_PHYSICS_FORCE, 0, &pos);
			info.SetDamageCustom(TF_DMG_CUSTOM_TELEFRAG);
			player->TakeDamage(info);
		}
	}

	DETOUR_DECL_MEMBER(int, CTFPlayer_OnTakeDamage, CTakeDamageInfo &info)
	{
		int damage = DETOUR_MEMBER_CALL(CTFPlayer_OnTakeDamage)(info);

		//Non sniper rifle explosive headshot
		auto weapon = info.GetWeapon();
		if (weapon != nullptr && info.GetAttacker() != nullptr && (info.GetDamageCustom() == TF_DMG_CUSTOM_HEADSHOT || info.GetDamageCustom() == TF_DMG_CUSTOM_HEADSHOT_DECAPITATION)) {
			auto tfweapon = rtti_cast<CTFWeaponBase *>(weapon->MyCombatWeaponPointer());
			auto player = reinterpret_cast<CTFPlayer *>(this);
			auto attacker = ToTFPlayer(info.GetAttacker());

			if (tfweapon != nullptr && !WeaponID_IsSniperRifle(tfweapon->GetWeaponID())) {
				int iExplosiveShot = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER ( tfweapon, iExplosiveShot, explosive_sniper_shot );
				if (iExplosiveShot != 0) {
					reinterpret_cast<CTFSniperRifle *>(tfweapon)->ExplosiveHeadShot(attacker, player);
				}
			}
		}
		return damage;
	}

	DETOUR_DECL_MEMBER(bool, CSchemaAttributeType_Default_ConvertStringToEconAttributeValue , const CEconItemAttributeDefinition *pAttrDef, const char *pszValue, attribute_data_union_t *out_pValue, bool floatforce)
	{
		if (pszValue[0] == 'i' || pszValue[0] == 'I') {
			out_pValue->m_UInt = (uint32)V_atoui64(pszValue + 1);
			return true;
		}
		else if (pszValue[0] == 'x' || pszValue[0] == 'X') {
			out_pValue->m_UInt = (uint32)strtoll(pszValue + 1, nullptr, 16);
			return true;
		}

		return DETOUR_MEMBER_CALL(CSchemaAttributeType_Default_ConvertStringToEconAttributeValue)(pAttrDef, pszValue, out_pValue, floatforce);
	}

	DETOUR_DECL_MEMBER(bool, static_attrib_t_BInitFromKV_SingleLine, const char *context, KeyValues *attribute, CUtlVector<CUtlString> *errors, bool b)
	{
		if (V_strnicmp(attribute->GetName(), "SET BONUS: ", strlen("SET BONUS: ")) == 0) {
			attribute->SetName(attribute->GetName() + strlen("SET BONUS: "));
		}

		return DETOUR_MEMBER_CALL(static_attrib_t_BInitFromKV_SingleLine)(context, attribute, errors, b);
	}

	DETOUR_DECL_MEMBER(bool, CTraceFilterObject_ShouldHitEntity, IHandleEntity *pServerEntity, int contentsMask)
	{
		CTraceFilterSimple *filter = reinterpret_cast<CTraceFilterSimple*>(this);

		bool result = DETOUR_MEMBER_CALL(CTraceFilterObject_ShouldHitEntity)(pServerEntity, contentsMask);
		
		if (result) {
			CBaseEntity *entityme = const_cast< CBaseEntity * >(EntityFromEntityHandle(filter->GetPassEntity()));
			CBaseEntity *entityhit = EntityFromEntityHandle(pServerEntity);

			bool entityme_player = entityme->IsPlayer();
			bool entityhit_player = entityhit->IsPlayer();

			if (!entityme_player || (!entityhit_player && !entityhit->IsBaseObject()))
				return true;

			bool me_collide = true;
			bool hit_collide = true;

			int not_solid = GetFastAttributeInt(entityme, 0, NOT_SOLID_TO_PLAYERS);
			me_collide = not_solid == 0;

			if (!me_collide)
				return false;

			if (entityhit_player) {
				int not_solid = GetFastAttributeInt(entityhit, 0, NOT_SOLID_TO_PLAYERS);
				hit_collide = not_solid == 0;
			}

			return hit_collide;
		}	 

		return result;
	}

	DETOUR_DECL_MEMBER(void, CTFPlayer_CancelTaunt)
	{
		int iCancelTaunt = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER ( reinterpret_cast<CTFPlayer *>(this), iCancelTaunt, always_allow_taunt );
		if (iCancelTaunt != 0) {
			return;
		}
		return DETOUR_MEMBER_CALL(CTFPlayer_CancelTaunt)();
	}

	DETOUR_DECL_MEMBER(bool, CTFPlayer_IsAllowedToInitiateTauntWithPartner, CEconItemView *pEconItemView, char *pszErrorMessage, int cubErrorMessage)
	{
		int iAllowTaunt = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER ( reinterpret_cast<CTFPlayer *>(this), iAllowTaunt, always_allow_taunt );
		if (iAllowTaunt != 0) {
			return true;
		}
		return DETOUR_MEMBER_CALL(CTFPlayer_IsAllowedToInitiateTauntWithPartner)( pEconItemView, pszErrorMessage, cubErrorMessage);
	}

	DETOUR_DECL_MEMBER(bool, CTFWeaponBase_DeflectEntity, CBaseEntity *pTarget, CTFPlayer *pOwner, Vector &vecForward, Vector &vecCenter, Vector &vecSize)
	{
		int team = pTarget->GetTeamNumber();
		CBaseEntity *projOwner = pTarget->GetOwnerEntity();
		auto result = DETOUR_MEMBER_CALL(CTFWeaponBase_DeflectEntity)(pTarget, pOwner, vecForward, vecCenter, vecSize);
		if (result) {
			int deflectKeepTeam = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER ( reinterpret_cast<CTFWeaponBase *>(this), deflectKeepTeam, reflect_keep_team );
			if (deflectKeepTeam != 0) {
				pTarget->SetTeamNumber(team);
				pTarget->SetOwnerEntity(projOwner);
			}

			int reflectMagnet = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(reinterpret_cast<CTFWeaponBase *>(this), reflectMagnet, reflect_magnet);
			if (reflectMagnet != 0) {
				IPhysicsObject *physics = pTarget->VPhysicsGetObject();

				if (physics != nullptr) {
					AngularImpulse ang_imp;
					Vector vel;
					physics->GetVelocity(&vel, &ang_imp);
					float len = vel.Length();
					vel = pOwner->WorldSpaceCenter() - pTarget->GetAbsOrigin();
					vel.NormalizeInPlace();
					vel *= len;
					physics->SetVelocity(&vel, &ang_imp);
				}
				else {
					Vector vel = pTarget->GetAbsVelocity();
					float len = vel.Length();
					vel = pOwner->WorldSpaceCenter() - pTarget->GetAbsOrigin();
					vel.NormalizeInPlace();
					vel *= len;
					pTarget->SetAbsVelocity(vel);
				}
			}

			float deflectStrength = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(reinterpret_cast<CTFWeaponBase *>(this), deflectStrength, mult_reflect_velocity);
			if (deflectStrength != 1.0f) {
				IPhysicsObject *physics = pTarget->VPhysicsGetObject();

				if (physics != nullptr) {
					AngularImpulse ang_imp;
					Vector vel;
					physics->GetVelocity(&vel, &ang_imp);
					vel *= deflectStrength;
					physics->SetVelocity(&vel, &ang_imp);
				}
				else {
					Vector vel = pTarget->GetAbsVelocity();
					vel *= deflectStrength;
					pTarget->SetAbsVelocity(vel);
				}

			}
		}
		return result;
	}

	DETOUR_DECL_MEMBER(const char *, CTFGameRules_GetKillingWeaponName, const CTakeDamageInfo &info, CTFPlayer *pVictim, int *iWeaponID)
	{
		if (info.GetWeapon() != nullptr && info.GetAttacker() != nullptr && info.GetAttacker()->IsPlayer()) {
			CBaseCombatWeapon *weapon = info.GetWeapon()->MyCombatWeaponPointer();
			if (weapon != nullptr && weapon->GetItem() != nullptr) {
				
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "custom kill icon", str);
				if (str != nullptr)
					return str;
			}
		}
		return DETOUR_MEMBER_CALL(CTFGameRules_GetKillingWeaponName)(info, pVictim, iWeaponID);
	}

	std::unordered_map<CTFPlayer *, std::unordered_map<CBaseEntity *, float>> player_touch_times;
	DETOUR_DECL_MEMBER(void, CTFPlayer_Touch, CBaseEntity *toucher)
	{
		DETOUR_MEMBER_CALL(CTFPlayer_Touch)(toucher);
		auto player = reinterpret_cast<CTFPlayer *>(this);

		if (toucher->GetTeamNumber() == player->GetTeamNumber()) return;

		if (toucher->IsBaseObject()) {
			float stomp = GetFastAttributeFloat(player, 0.0f, STOMP_BUILDING_DAMAGE);
			if (stomp != 0.0f) {
				
				float stompTime = GetFastAttributeFloat(player, 0.0f, STOMP_PLAYER_TIME);

				if (stompTime == 0.0f || (gpGlobals->curtime - player_touch_times[player][toucher]) > stompTime) {
					float stomp = 0.0f;
					CTakeDamageInfo info(player, player, nullptr, vec3_origin, vec3_origin, stomp, DMG_BLAST);
					toucher->TakeDamage(info);
					if (stompTime != 0.0f)
						player_touch_times[player][toucher] = gpGlobals->curtime;
				}
			}
		}
		else if (toucher->IsPlayer()) {
			
			float stompTime = GetFastAttributeFloat(player, 0.0f, STOMP_PLAYER_TIME);

			if (stompTime == 0.0f || (gpGlobals->curtime - player_touch_times[player][toucher]) > stompTime) {
				float stomp = GetFastAttributeFloat(player, 0.0f, STOMP_PLAYER_DAMAGE);
				if (stomp != 0.0f) {
					CTakeDamageInfo info(player, player, nullptr, vec3_origin, vec3_origin, stomp, DMG_BLAST);
					toucher->TakeDamage(info);
				}

				float knockback = GetFastAttributeFloat(player, 0.0f, STOMP_PLAYER_FORCE);
				if (knockback != 0.0f) {
					Vector vec = toucher->GetAbsOrigin() - player->GetAbsOrigin();
					vec.NormalizeInPlace();
					vec.z = 1.0f;
					vec *= knockback;
					ToTFPlayer(toucher)->ApplyGenericPushbackImpulse(vec, player);
				}
				if (stompTime != 0.0f)
					player_touch_times[player][toucher] = gpGlobals->curtime;
			}
		}
	}

	void InspectAttributes(CTFPlayer *target, CTFPlayer *player, bool force, int slot = -2);

	DETOUR_DECL_MEMBER(void, CUpgrades_PlayerPurchasingUpgrade, CTFPlayer *player, int itemslot, int upgradeslot, bool sell, bool free, bool refund)
	{
		if (!refund) {
			auto upgrade = reinterpret_cast<CUpgrades *>(this);
			
			if (itemslot >= 0 && upgradeslot >= 0 && upgradeslot < CMannVsMachineUpgradeManager::Upgrades().Count()) {
				
				CEconEntity *entity = GetEconEntityAtLoadoutSlot(player, itemslot);

				if (entity != nullptr) {
					int iCannotUpgrade = 0;
					CALL_ATTRIB_HOOK_INT_ON_OTHER ( entity, iCannotUpgrade, cannot_be_upgraded );
					if (iCannotUpgrade > 0) {
						if (!sell) {
							gamehelpers->TextMsg(ENTINDEX(player), TEXTMSG_DEST_CENTER, "This weapon is not upgradeable");
						}
						return;
					}
				}
			}
		}
		DETOUR_MEMBER_CALL(CUpgrades_PlayerPurchasingUpgrade)(player, itemslot, upgradeslot, sell, free, refund);
		
		if (!refund) {
			InspectAttributes(player, player , true, itemslot);
		}

		// Delete old dropped weapons if a player refunded an upgrade
		if (sell && !refund) {
			if (itemslot >= 0) {
				CEconEntity *entity = GetEconEntityAtLoadoutSlot(player, itemslot);
				if (entity != nullptr) {
					ForEachEntityByRTTI<CTFDroppedWeapon>([&](CTFDroppedWeapon *weapon) {
						if (weapon->m_Item->m_iItemID == entity->GetItem()->m_iItemID) {
							weapon->Remove();
						}
					});
				}
			}
		}
	}
	
	RefCount rc_CTFPlayer_ReapplyItemUpgrades;
	DETOUR_DECL_MEMBER(void, CTFPlayer_ReapplyItemUpgrades, CEconItemView *item_view)
	{
		auto player = reinterpret_cast<CTFPlayer *>(this);
		bool no_upgrade = false;
		if (item_view != nullptr) {
			auto attr = item_view->GetAttributeList().GetAttributeByName("cannot be upgraded");
			no_upgrade = attr != nullptr && attr->GetValuePtr()->m_Float > 0.0f;
		}
		SCOPED_INCREMENT_IF(rc_CTFPlayer_ReapplyItemUpgrades, no_upgrade);
		DETOUR_MEMBER_CALL(CTFPlayer_ReapplyItemUpgrades)(item_view);
	}

	DETOUR_DECL_MEMBER(attrib_definition_index_t, CUpgrades_ApplyUpgradeToItem, CTFPlayer* player, CEconItemView *item, int upgrade, int cost, bool downgrade, bool fresh)
	{
		if (rc_CTFPlayer_ReapplyItemUpgrades)
		{
			return INVALID_ATTRIB_DEF_INDEX;
		}

        return DETOUR_MEMBER_CALL(CUpgrades_ApplyUpgradeToItem)(player, item, upgrade, cost, downgrade, fresh);
    }

	bool IsDeflectable(CBaseProjectile *proj) {
		if (proj->GetOriginalLauncher() == nullptr)
			return true;

		int iCannotReflect = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(proj->GetOriginalLauncher(), iCannotReflect, projectile_no_deflect);
		if (iCannotReflect != 0)
			return false;

		return true;
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_Rocket_IsDeflectable)
	{
		auto ent = reinterpret_cast<CTFProjectile_Rocket *>(this);

		return DETOUR_MEMBER_CALL(CTFProjectile_Rocket_IsDeflectable)() && IsDeflectable(ent);
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_Arrow_IsDeflectable)
	{
		auto ent = reinterpret_cast<CTFProjectile_Arrow *>(this);

		return DETOUR_MEMBER_CALL(CTFProjectile_Arrow_IsDeflectable)() && IsDeflectable(ent);
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_Flare_IsDeflectable)
	{
		auto ent = reinterpret_cast<CTFProjectile_Flare *>(this);

		return DETOUR_MEMBER_CALL(CTFProjectile_Flare_IsDeflectable)() && IsDeflectable(ent);
	}

	DETOUR_DECL_MEMBER(bool, CTFProjectile_EnergyBall_IsDeflectable)
	{
		auto ent = reinterpret_cast<CTFProjectile_EnergyBall *>(this);

		return DETOUR_MEMBER_CALL(CTFProjectile_EnergyBall_IsDeflectable)() && IsDeflectable(ent);
	}

	DETOUR_DECL_MEMBER(bool, CTFGrenadePipebombProjectile_IsDeflectable)
	{
		auto ent = reinterpret_cast<CTFGrenadePipebombProjectile *>(this);

		return DETOUR_MEMBER_CALL(CTFGrenadePipebombProjectile_IsDeflectable)() && IsDeflectable(ent);
	}

	// Stop short circuit from deflecting the projectile
	
	RefCount rc_CTFProjectile_MechanicalArmOrb_CheckForProjectiles;
	DETOUR_DECL_MEMBER(void, CTFProjectile_MechanicalArmOrb_CheckForProjectiles)
	{
		SCOPED_INCREMENT(rc_CTFProjectile_MechanicalArmOrb_CheckForProjectiles);
		DETOUR_MEMBER_CALL(CTFProjectile_MechanicalArmOrb_CheckForProjectiles)();
	}

	RefCount rc_CTFProjectile_Arrow_ArrowTouch;

	DETOUR_DECL_MEMBER(bool, CBaseEntity_InSameTeam, CBaseEntity *other)
	{
		auto ent = reinterpret_cast<CBaseEntity *>(this);
		if (rc_CTFProjectile_MechanicalArmOrb_CheckForProjectiles)
		{
			if (!IsDeflectable(static_cast<CBaseProjectile *>(ent))) {
				return true;
			}
		}
/*
		if ((ent->m_fFlags & FL_GRENADE) && (other->m_fFlags & FL_GRENADE) && strncmp(ent->GetClassname(), "tf_projectile", strlen("tf_projectile")) == 0) {
			if (!IsDeflectable(static_cast<CBaseProjectile *>(ent))) {
				return true;
			}
		}*/

		return DETOUR_MEMBER_CALL(CBaseEntity_InSameTeam)(other);
	}

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_OnAddBalloonHead)
	{
        DETOUR_MEMBER_CALL(CTFPlayerShared_OnAddBalloonHead)();
		CTFPlayer *player = reinterpret_cast<CTFPlayerShared *>(this)->GetOuter();

		float gravity = 0.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(player, gravity, player_gravity_ballon_head);
		if (gravity != 0.0f)
			player->SetGravity(gravity);
	}

	DETOUR_DECL_MEMBER(void, CTFWeaponBase_ApplyOnHitAttributes, CBaseEntity *ent, CTFPlayer *player, const CTakeDamageInfo& info)
	{
		DETOUR_MEMBER_CALL(CTFWeaponBase_ApplyOnHitAttributes)(ent, player, info);
		
		auto weapon = static_cast<CTFWeaponBase *>(ToBaseCombatWeapon(info.GetWeapon()));
		if (ent != nullptr && weapon != nullptr) {
			GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "custom hit sound", str);
			if (str != nullptr) {
				PrecacheSound(str);
				ent->EmitSound(str);
			}
			
			CTFPlayer *victim = ToTFPlayer(ent);
			if (victim != nullptr && victim != player) {
				float burn_duration = 0.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, burn_duration, set_dmgtype_ignite);
				if (burn_duration != 0.0f) {
					victim->m_Shared->Burn(player, nullptr, burn_duration);
				}

				int removecond_attr = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, removecond_attr, remove_cond_on_hit);
				if (removecond_attr != 0) {
					for (int i = 0; i < 4; i++) {
						int removecond = (removecond_attr >> (i * 8)) & 255;
						if (removecond != 0) {
							victim->m_Shared->RemoveCond((ETFCond)removecond);
						}
					}
				}

				int addcond_attr = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, addcond_attr, add_cond_on_hit);
				if (addcond_attr != 0) {
					float addcond_duration = 0.0f;
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, addcond_duration, add_cond_on_hit_duration);
					if (addcond_duration == 0.0f) {
						addcond_duration = -1.0f;
					}
					for (int i = 0; i < 4; i++) {
						int addcond = (addcond_attr >> (i * 8)) & 255;
						if (addcond != 0) {
							victim->m_Shared->AddCond((ETFCond)addcond, addcond_duration, player);
						}
					}
				}

				int self_addcond_attr = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, self_addcond_attr, self_cond_on_hit);
				if (self_addcond_attr != 0) {
					float addcond_duration = 0.0f;
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, addcond_duration, self_add_cond_on_hit_duration);
					if (addcond_duration == 0.0f) {
						addcond_duration = -1.0f;
					}
					for (int i = 0; i < 4; i++) {
						int addcond = (self_addcond_attr >> (i * 8)) & 255;
						if (addcond != 0) {
							player->m_Shared->AddCond((ETFCond)addcond, addcond_duration, player);
						}
					}
				}
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CObjectSentrygun_MakeScaledBuilding, CTFPlayer *player)
	{
		auto sentry = reinterpret_cast<CObjectSentrygun *>(this);
		//DevMsg("sentry deploy %d %d\n", sentry->m_bCarryDeploy + 0, sentry->m_bCarried + 0);
		if (sentry->m_bCarried)
			return;
		DETOUR_MEMBER_CALL(CObjectSentrygun_MakeScaledBuilding)(player);
		
	}

	DETOUR_DECL_MEMBER(void, CObjectTeleporter_TeleporterTouch, CBaseEntity *player)
	{
		auto tele = reinterpret_cast<CObjectTeleporter *>(this);
		
		if (player->IsPlayer()) {
			int iCannotTeleport = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(player, iCannotTeleport, cannot_be_teleported);
			if (iCannotTeleport != 0)
				return;
		}
		DETOUR_MEMBER_CALL(CObjectTeleporter_TeleporterTouch)(player);
	}

	DETOUR_DECL_MEMBER(float, CWeaponMedigun_GetTargetRange)
	{
		auto weapon = reinterpret_cast<CWeaponMedigun *>(this);
		
		float range = DETOUR_MEMBER_CALL(CWeaponMedigun_GetTargetRange)();
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, range, mult_medigun_range);
		return range;
	}
	
	DETOUR_DECL_MEMBER(bool, CTFBotTacticalMonitor_ShouldOpportunisticallyTeleport, CTFPlayer *bot)
	{
		bool result = DETOUR_MEMBER_CALL(CTFBotTacticalMonitor_ShouldOpportunisticallyTeleport)(bot);
		if (result) {
			int iCannotTeleport = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(bot, iCannotTeleport, cannot_be_teleported);
			result = iCannotTeleport == 0;
		}
		return result;
	}
	

	DETOUR_DECL_MEMBER(void, CTFProjectile_Arrow_ArrowTouch, CBaseEntity *pOther)
	{
		auto arrow = reinterpret_cast<CTFProjectile_Arrow *>(this);

		if (pOther->IsBaseObject() && pOther->GetTeamNumber() == arrow->GetTeamNumber() && ToBaseObject(pOther)->HasSapper()) {
			int can_damage_sappers = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(arrow->GetOriginalLauncher(), can_damage_sappers, set_dmg_apply_to_sapper);

			if (can_damage_sappers != 0) {
				if (rtti_cast<CObjectSapper *>(pOther->FirstMoveChild()) != nullptr) {
					pOther = pOther->FirstMoveChild();
					DevMsg("Set other entity\n");
				}
			}
		}

		SCOPED_INCREMENT(rc_CTFProjectile_Arrow_ArrowTouch);
		DETOUR_MEMBER_CALL(CTFProjectile_Arrow_ArrowTouch)(pOther);

		int iPenetrateLimit = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(arrow->GetOriginalLauncher(), iPenetrateLimit, projectile_penetration_limit);
		if (iPenetrateLimit != 0 && arrow->m_HitEntities->Count() >= iPenetrateLimit + 1) {
			arrow->Remove();
		}
	}

	DETOUR_DECL_MEMBER(int, CTFRadiusDamageInfo_ApplyToEntity, CBaseEntity *ent)
	{
		auto info = reinterpret_cast<CTFRadiusDamageInfo *>(this);
		if (hit_entities_explosive_max != 0 && hit_entities_explosive >= hit_entities_explosive_max)
			return 0;
		int healthpre = ent->GetHealth();
		//DevMsg("Applytoentity damage %f %d\n", info->m_DmgInfo->GetDamage(), ent->CollisionProp()->IsPointInBounds(info->m_vecOrigin));
		auto result = DETOUR_MEMBER_CALL(CTFRadiusDamageInfo_ApplyToEntity)(ent);
		if (ent->GetHealth() != healthpre) {
			hit_entities_explosive++;
		}
		return result;
	}
	
	std::map<CHandle<CBaseEntity>, int> entity_penetration_counter;
	
	DETOUR_DECL_MEMBER(bool, CTFFlameManager_BCanBurnEntityThisFrame, CBaseEntity* entity)
	{
		auto flamemgr = reinterpret_cast<CBaseEntity *>(this);

		bool ret = DETOUR_MEMBER_CALL(CTFFlameManager_BCanBurnEntityThisFrame)(entity);
		if (ret) {
			int iMaxAoe = GetFastAttributeInt(flamemgr->GetOwnerEntity(), 0, MAX_AOE_TARGETS);
			if (iMaxAoe != 0) {
				int &counter = entity_penetration_counter[flamemgr];
				int min_delay;
				switch(iMaxAoe) {
					case 1: min_delay = 5; break;
					case 2: min_delay = 3; break;
					case 3: min_delay = 2; break;
					default: min_delay = 1; 
				}
				
				ret = gpGlobals->tickcount - counter >= min_delay;
				if (ret)
					counter = gpGlobals->tickcount;
			}
		}
		return ret;
	}

	DETOUR_DECL_MEMBER(void, CTFProjectile_BallOfFire_RocketTouch, CBaseEntity *pOther)
	{
		auto arrow = reinterpret_cast<CBaseProjectile *>(this);

		if (pOther == nullptr) {
			DETOUR_MEMBER_CALL(CTFProjectile_BallOfFire_RocketTouch)(pOther);
			return;
		}

		int health_pre = pOther->GetHealth();
		DETOUR_MEMBER_CALL(CTFProjectile_BallOfFire_RocketTouch)(pOther);
		
		if (pOther != arrow && health_pre != pOther->GetHealth()) {
			int &counter = entity_penetration_counter[arrow];
			counter += 1;
			int iPenetrateLimit = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(arrow->GetOriginalLauncher(), iPenetrateLimit, projectile_penetration_limit);
			if (iPenetrateLimit != 0 && counter >= iPenetrateLimit) {
				arrow->Remove();
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CTFProjectile_EnergyRing_ProjectileTouch, CBaseEntity *pOther)
	{
		auto arrow = reinterpret_cast<CBaseProjectile *>(this);

		if (pOther == nullptr) {
			DETOUR_MEMBER_CALL(CTFProjectile_EnergyRing_ProjectileTouch)(pOther);
			return;
		}

		int health_pre = pOther->GetHealth();
		DETOUR_MEMBER_CALL(CTFProjectile_EnergyRing_ProjectileTouch)(pOther);	
		
		if (pOther != arrow && health_pre != pOther->GetHealth()) {
			int &counter = entity_penetration_counter[arrow];
			counter += 1;
			int iPenetrateLimit = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(arrow->GetOriginalLauncher(), iPenetrateLimit, projectile_penetration_limit);
			if (iPenetrateLimit != 0 && counter >= iPenetrateLimit) {
				arrow->Remove();
			}
		}
	}

	RefCount rc_CTFPlayerShared_AddCond;
	RefCount rc_CTFPlayerShared_RemoveCond;
	CBaseEntity *addcond_provider = nullptr;
	CBaseEntity *addcond_provider_item = nullptr;

	int aoe_in_sphere_max_hit_count = 0;
	int aoe_in_sphere_hit_count = 0;

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_AddCond, ETFCond nCond, float flDuration, CBaseEntity *pProvider)
	{
		CTFPlayer *player = reinterpret_cast<CTFPlayerShared *>(this)->GetOuter();

		if (pProvider != player && (nCond == TF_COND_URINE || nCond == TF_COND_MAD_MILK || nCond == TF_COND_MARKEDFORDEATH || nCond == TF_COND_MARKEDFORDEATH_SILENT)) {
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(player, flDuration, mult_debuff_duration);
		}

		if (rc_CTFPlayerShared_AddCond)
        {
			if (aoe_in_sphere_max_hit_count != 0 && ++aoe_in_sphere_hit_count > aoe_in_sphere_max_hit_count) {
				return;
			}

			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(addcond_provider, flDuration, mult_effect_duration);
			int iCondOverride = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(addcond_provider, iCondOverride, effect_cond_override);

			DevMsg("add cond pre %d\n", iCondOverride);
			// Allow up to 4 addconds with bit shifting
			if (iCondOverride != 0) {
				for (int i = 0; i < 4; i++) {
					int addcond = (iCondOverride >> (i * 8)) & 255;
					DevMsg("add cond post %d\n", addcond);
					if (addcond != 0) {
						nCond = (ETFCond) addcond;
						DETOUR_MEMBER_CALL(CTFPlayerShared_AddCond)(nCond, flDuration, pProvider);
					}
				}
				return;
			}

			auto weapon = ToBaseCombatWeapon(addcond_provider_item);
			Msg(CFmtStr("provider item, %d\n", weapon));
			if (weapon != nullptr) {
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "effect add attributes", attribs);
				
				if (attribs != nullptr) {
					std::string str(attribs);
					Msg(CFmtStr("attribs, %s\n", attribs));
					boost::tokenizer<boost::char_separator<char>> tokens(str, boost::char_separator<char>("|"));

					auto it = tokens.begin();
					while (it != tokens.end()) {
						auto attribute = *it;
						if (++it == tokens.end())
							break;
						auto &value = *it;
						Msg(CFmtStr("provide, %s %f %f\n", attribute.c_str(), strtof(value.c_str(),nullptr), flDuration));
						player->AddCustomAttribute(attribute.c_str(), strtof(value.c_str(),nullptr), flDuration);
						it++;
					}
				}
			}
        }
		DETOUR_MEMBER_CALL(CTFPlayerShared_AddCond)(nCond, flDuration, pProvider);
	}

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_RemoveCond, ETFCond nCond, bool bool1)
	{
		if (rc_CTFPlayerShared_RemoveCond)
        {
			CTFPlayer *player = reinterpret_cast<CTFPlayerShared *>(this)->GetOuter();
			int iCondOverride = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(addcond_provider, iCondOverride, effect_cond_override);

			// Allow up to 4 addconds with bit shifting
			if (iCondOverride != 0) {
				for (int i = 0; i < 4; i++) {
					int addcond = (iCondOverride >> (i * 8)) & 255;
					if (addcond != 0) {
						nCond = (ETFCond) addcond;
						DETOUR_MEMBER_CALL(CTFPlayerShared_RemoveCond)(nCond, bool1);
					}
				}
				return;
			}

			auto weapon = ToBaseCombatWeapon(addcond_provider_item);
			Msg(CFmtStr("remove cond item, %d\n", weapon));
			if (weapon != nullptr) {
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "effect add attributes", attribs);
				
				if (attribs != nullptr) {
					std::string str(attribs);
					Msg(CFmtStr("attribs, %s\n", attribs));
					boost::tokenizer<boost::char_separator<char>> tokens(str, boost::char_separator<char>("|"));

					auto it = tokens.begin();
					while (it != tokens.end()) {
						auto attribute = *it;
						if (++it == tokens.end())
							break;
						auto &value = *it;
						Msg(CFmtStr("provide, %s %f\n", attribute.c_str()));
						player->RemoveCustomAttribute(attribute.c_str());
						it++;
					}
				}
			}
        }
		DETOUR_MEMBER_CALL(CTFPlayerShared_RemoveCond)(nCond, bool1);
	}

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_PulseRageBuff, int rage)
	{
		SCOPED_INCREMENT(rc_CTFPlayerShared_AddCond);
		CTFPlayer *player = addcond_provider = reinterpret_cast<CTFPlayerShared *>(this)->GetOuter();
		addcond_provider_item = GetEconEntityAtLoadoutSlot(player, rage == 5 ? LOADOUT_POSITION_PRIMARY : LOADOUT_POSITION_SECONDARY);

		DETOUR_MEMBER_CALL(CTFPlayerShared_PulseRageBuff)(rage);
	}

	DETOUR_DECL_MEMBER(void, CTFPlayer_DoTauntAttack)
	{
		SCOPED_INCREMENT(rc_CTFPlayerShared_AddCond);
		addcond_provider = reinterpret_cast<CTFPlayer *>(this);
		addcond_provider_item = GetEconEntityAtLoadoutSlot(reinterpret_cast<CTFPlayer *>(this), LOADOUT_POSITION_SECONDARY);
		DETOUR_MEMBER_CALL(CTFPlayer_DoTauntAttack)();
	}

	#define WEAPON_USE_DETOUR \
	SCOPED_INCREMENT(rc_CTFPlayerShared_AddCond); \
	SCOPED_INCREMENT(rc_CTFPlayerShared_RemoveCond); \
	addcond_provider = reinterpret_cast<CBaseEntity *>(this)->GetOwnerEntity(); \
	addcond_provider_item = reinterpret_cast<CBaseEntity *>(this); \
	
	DETOUR_DECL_MEMBER(void, CTFSodaPopper_SecondaryAttack)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFSodaPopper_SecondaryAttack)();
	}

	DETOUR_DECL_STATIC(void, JarExplode, int iEntIndex, CTFPlayer *pAttacker, CBaseEntity *pOriginalWeapon, CBaseEntity *pWeapon, const Vector& vContactPoint, int iTeam, float flRadius, ETFCond cond, float flDuration, const char *pszImpactEffect, const char *text2 )
	{
		SCOPED_INCREMENT(rc_CTFPlayerShared_AddCond);
		addcond_provider = pAttacker;
		addcond_provider_item = pOriginalWeapon;
		
		CBaseCombatWeapon *econ_entity = ToBaseCombatWeapon(pOriginalWeapon);
		if (econ_entity != nullptr) {

			aoe_in_sphere_max_hit_count = GetFastAttributeInt(econ_entity, 0, MAX_AOE_TARGETS);
			aoe_in_sphere_hit_count = 0;

			GET_STRING_ATTRIBUTE(econ_entity->GetItem()->GetAttributeList(), "explosion particle", particlename);
			if (particlename != nullptr) {
				pszImpactEffect = particlename;
			}
			
			GET_STRING_ATTRIBUTE(econ_entity->GetItem()->GetAttributeList(), "custom impact sound", sound);
			if (sound != nullptr) {
				PrecacheSound(sound);
				text2 = sound;
			}
		}

		DETOUR_STATIC_CALL(JarExplode)(iEntIndex, pAttacker, pOriginalWeapon, pWeapon, vContactPoint, iTeam, flRadius, cond, flDuration, pszImpactEffect, text2);
		aoe_in_sphere_max_hit_count = 0;
	}
	DETOUR_DECL_MEMBER(void, CTFGasManager_OnCollide, CBaseEntity *entity, int id )
	{
		SCOPED_INCREMENT(rc_CTFPlayerShared_AddCond);
		addcond_provider_item = addcond_provider = reinterpret_cast<CBaseEntity *>(this)->GetOwnerEntity();
		DETOUR_MEMBER_CALL(CTFGasManager_OnCollide)(entity, id);
	}

	struct MedigunEffects_t
	{
		ETFCond eCondition;
		ETFCond eWearingOffCondition;
		const char *pszChargeOnSound;
		const char *pszChargeOffSound;
	};

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_SetChargeEffect, int iCharge, bool bState, bool bInstant, MedigunEffects_t& effects, float flWearOffTime, CTFPlayer *pProvider)
	{
		addcond_provider = pProvider;
		addcond_provider_item = pProvider != nullptr ? GetEconEntityAtLoadoutSlot(pProvider, LOADOUT_POSITION_SECONDARY) : nullptr;
		int iCondOverride = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(addcond_provider, iCondOverride, effect_cond_override);
		
		SCOPED_INCREMENT_IF(rc_CTFPlayerShared_AddCond, iCondOverride != 0);
		SCOPED_INCREMENT_IF(rc_CTFPlayerShared_RemoveCond, iCondOverride != 0);
		
		ETFCond old_cond = effects.eCondition;
		ETFCond old_wearing_cond = effects.eWearingOffCondition;
		
		if (iCondOverride != 0) {
			effects.eCondition = (ETFCond) (iCondOverride & 255);
			effects.eWearingOffCondition = (ETFCond) TF_COND_COUNT;
		}
		DETOUR_MEMBER_CALL(CTFPlayerShared_SetChargeEffect)(iCharge, bState, bInstant, effects, flWearOffTime, pProvider);
		if (iCondOverride != 0) {
			effects.eCondition = old_cond;
			effects.eWearingOffCondition = old_wearing_cond;
		}
	}

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_PulseMedicRadiusHeal)
	{
		SCOPED_INCREMENT(rc_CTFPlayerShared_AddCond);
		addcond_provider = reinterpret_cast<CTFPlayerShared *>(this)->GetOuter();
		addcond_provider_item = GetEconEntityAtLoadoutSlot(reinterpret_cast<CTFPlayerShared *>(this)->GetOuter(), LOADOUT_POSITION_MELEE);
		DETOUR_MEMBER_CALL(CTFPlayerShared_PulseMedicRadiusHeal)();
	}

	DETOUR_DECL_MEMBER(void, CTFPlayerShared_SetRevengeCrits, int crits)
	{
		SCOPED_INCREMENT(rc_CTFPlayerShared_AddCond);
		SCOPED_INCREMENT(rc_CTFPlayerShared_RemoveCond);
		addcond_provider = reinterpret_cast<CTFPlayerShared *>(this)->GetOuter();
		addcond_provider_item = reinterpret_cast<CTFPlayerShared *>(this)->GetOuter()->GetActiveWeapon();
		DETOUR_MEMBER_CALL(CTFPlayerShared_SetRevengeCrits)(crits);
	}
	
	DETOUR_DECL_MEMBER(void, CTFFlareGun_Revenge_Holster, CBaseCombatWeapon *weapon)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFFlareGun_Revenge_Holster)(weapon);
	}

	DETOUR_DECL_MEMBER(void, CTFShotgun_Revenge_Holster, CBaseCombatWeapon *weapon)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFShotgun_Revenge_Holster)(weapon);
	}

	DETOUR_DECL_MEMBER(void, CTFRevolver_Holster, CBaseCombatWeapon *weapon)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFRevolver_Holster)(weapon);
	}

	DETOUR_DECL_MEMBER(void, CTFFlareGun_Revenge_Deploy)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFFlareGun_Revenge_Deploy)();
	}

	DETOUR_DECL_MEMBER(void, CTFShotgun_Revenge_Deploy)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFShotgun_Revenge_Deploy)();
	}

	DETOUR_DECL_MEMBER(void, CTFRevolver_Deploy)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFRevolver_Deploy)();
	}

	DETOUR_DECL_MEMBER(void, CTFChargedSMG_SecondaryAttack)
	{
		WEAPON_USE_DETOUR
		DETOUR_MEMBER_CALL(CTFChargedSMG_SecondaryAttack)();
	}


	RefCount rc_AllowOverheal;
	DETOUR_DECL_MEMBER(void, CTFPlayer_OnKilledOther_Effects, CBaseEntity *other, const CTakeDamageInfo& info)
	{
		CBaseEntity *ent = info.GetWeapon();
		CTFPlayer *player = reinterpret_cast<CTFPlayer *>(this);
		int overheal_allow = 0;
		if (info.GetWeapon() != nullptr) {
			int addcond_attr = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(info.GetWeapon(), addcond_attr, add_cond_on_kill);
			if (addcond_attr != 0) {
				float addcond_duration = 0.0f;
				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(info.GetWeapon(), addcond_duration, add_cond_on_kill_duration);
				if (addcond_duration == 0.0f) {
					addcond_duration = -1.0f;
				}
				for (int i = 0; i < 4; i++) {
					int addcond = (addcond_attr >> (i * 8)) & 255;
					if (addcond != 0) {
						player->m_Shared->AddCond((ETFCond)addcond, addcond_duration, player);
					}
				}
			}
			CALL_ATTRIB_HOOK_INT_ON_OTHER(info.GetWeapon(), overheal_allow, overheal_from_heal_on_kill);
		}

		SCOPED_INCREMENT_IF(rc_AllowOverheal, overheal_allow != 0);

		DETOUR_MEMBER_CALL(CTFPlayer_OnKilledOther_Effects)(other, info);
	}

	DETOUR_DECL_MEMBER(float, CTFPlayer_TeamFortress_CalculateMaxSpeed, bool flag)
	{
		float ret = DETOUR_MEMBER_CALL(CTFPlayer_TeamFortress_CalculateMaxSpeed)(flag);

		CTFPlayer *player = reinterpret_cast<CTFPlayer *>(this);
		if (player->HasTheFlag())
		{
			float value = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(player, value, mult_flag_carrier_move_speed);
			if (value != 1.0f) {
				ret *= value;
			}
		}

		return ret;
	}
	
	DETOUR_DECL_MEMBER(void, CTFSniperRifle_ExplosiveHeadShot, CTFPlayer *player1, CTFPlayer *player2)
	{
		aoe_in_sphere_max_hit_count = GetFastAttributeInt(reinterpret_cast<CBaseEntity *>(this), 0, MAX_AOE_TARGETS);
		aoe_in_sphere_hit_count = 0;
		DETOUR_MEMBER_CALL(CTFSniperRifle_ExplosiveHeadShot)(player1, player2);
		aoe_in_sphere_max_hit_count = 0;
	}
	
	DETOUR_DECL_MEMBER(void, CTFPlayerShared_MakeBleed, CTFPlayer *attacker, CTFWeaponBase *weapon, float bleedTime, int bleeddmg, bool perm, int val)
	{
		if (aoe_in_sphere_max_hit_count != 0 && ++aoe_in_sphere_hit_count > aoe_in_sphere_max_hit_count) {
			return;
		}
		DETOUR_MEMBER_CALL(CTFPlayerShared_MakeBleed)(attacker, weapon, bleedTime, bleeddmg, perm, val);
	}

	DETOUR_DECL_MEMBER(int, CBaseEntity_TakeDamage, CTakeDamageInfo &info)
	{
		//DevMsg("Take damage damage %f\n", info.GetDamage());

		CBaseEntity *entity = reinterpret_cast<CBaseEntity *>(this);
		bool was_alive = entity->IsAlive();
		int damage = DETOUR_MEMBER_CALL(CBaseEntity_TakeDamage)(info);

		//Fire input on hit
		auto weapon = ToBaseCombatWeapon(info.GetWeapon());
		if (weapon != nullptr && info.GetAttacker() != nullptr && weapon->GetItem() != nullptr) {
			{
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "fire input on hit", input);
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "fire input on hit name restrict", filter);
				
				if (input != nullptr && (filter == nullptr || entity->NameMatches(filter))) {
					char input_tokenized[256];
					V_strncpy(input_tokenized, input, sizeof(input_tokenized));
					
					char *target = strtok(input_tokenized,"^");
					char *input = strtok(NULL,"^");
					char *param = strtok(NULL,"^");
					
					if (target != nullptr && input != nullptr) {
						variant_t variant1;
						if (param != nullptr) {
							string_t m_iParameter = AllocPooledString(param);
							variant1.SetString(m_iParameter);
						}
						else {
							variant1.SetInt(damage);
						}
						
						if (FStrEq(target, "!self")) {
							entity->AcceptInput(input,info.GetAttacker(), entity,variant1,-1);
						}
						else if (FStrEq(target, "!projectile") && info.GetInflictor() != nullptr) {
							info.GetInflictor()->AcceptInput(input,info.GetAttacker(), entity,variant1,-1);
						}
						else {
							CEventQueue &que = g_EventQueue;
							que.AddEvent(STRING(AllocPooledString(target)),STRING(AllocPooledString(input)),variant1,0,info.GetAttacker(), entity,-1);
						}
					}
				}
			}

			if (was_alive && !entity->IsAlive()) {
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "fire input on kill", input);
				GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "fire input on kill name restrict", filter);
				if (input != nullptr && (filter == nullptr || entity->NameMatches(filter))) {
					char input_tokenized[256];
					V_strncpy(input_tokenized, input, sizeof(input_tokenized));
					
					char *target = strtok(input_tokenized,"^");
					char *input = strtok(NULL,"^");
					char *param = strtok(NULL,"^");
					
					if (target != nullptr && input != nullptr) {
						variant_t variant1;
						if (param != nullptr) {
							string_t m_iParameter = AllocPooledString(param);
							variant1.SetString(m_iParameter);
						}
						else {
							variant1.SetInt(damage);
						}
						
						if (FStrEq(target, "!self")) {
							entity->AcceptInput(input,info.GetAttacker(), entity,variant1,-1);
						}
						else if (FStrEq(target, "!projectile") && info.GetInflictor() != nullptr) {
							info.GetInflictor()->AcceptInput(input,info.GetAttacker(), entity,variant1,-1);
						}
						else {
							CEventQueue &que = g_EventQueue;
							que.AddEvent(STRING(AllocPooledString(target)),STRING(AllocPooledString(input)),variant1,0,info.GetAttacker(), entity,-1);
						}
					}
				}
			}
		}
		return damage;
	}
	
	DETOUR_DECL_MEMBER(int, CTFPlayerShared_GetMaxBuffedHealth, bool flag1, bool flag2)
	{
		static ConVarRef tf_max_health_boost("tf_max_health_boost");
		float old_value = tf_max_health_boost.GetFloat();
		float value = old_value;
		value *= GetFastAttributeFloat(reinterpret_cast<CTFPlayerShared *>(this)->GetOuter(), 1.0f, MULT_MAX_OVERHEAL_SELF);
		
		auto ret = DETOUR_MEMBER_CALL(CTFPlayerShared_GetMaxBuffedHealth)(flag1, flag2);

		if (value != old_value)
			tf_max_health_boost.SetValue(old_value);
		return ret;
	}

	DETOUR_DECL_MEMBER(int, CTFPlayer_TakeHealth, float flHealth, int bitsDamageType)
	{
		if (rc_AllowOverheal > 0) {
			bitsDamageType |= DMG_IGNORE_MAXHEALTH;
		}
		
		return DETOUR_MEMBER_CALL(CTFPlayer_TakeHealth)(flHealth, bitsDamageType);
	}

	CTFPlayer *dispenser_owner = nullptr;
	DETOUR_DECL_MEMBER(int, CObjectDispenser_DispenseMetal, CTFPlayer *player)
	{
		dispenser_owner = reinterpret_cast<CObjectDispenser *>(this)->GetBuilder();
		auto ret = DETOUR_MEMBER_CALL(CObjectDispenser_DispenseMetal)(player);
		dispenser_owner = nullptr;
		return ret;
	}

	DETOUR_DECL_MEMBER(int, CTFPlayer_GiveAmmo, int amount, int type, bool sound, int source)
	{
		if (dispenser_owner != nullptr) {
			float mult = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(dispenser_owner, mult, mult_dispenser_rate);
			amount = amount * mult;
		}
		return DETOUR_MEMBER_CALL(CTFPlayer_GiveAmmo)(amount, type, sound, source);
	}
	
	struct StickInfo
	{
		CHandle<CBaseEntity> sticky;
		CHandle<CBaseEntity> sticked;
		Vector offset;
	};
	std::vector<StickInfo> stick_info;
	DETOUR_DECL_MEMBER(void, CTFGrenadePipebombProjectile_StickybombTouch, CBaseEntity *other)
	{
		auto proj = reinterpret_cast<CTFGrenadePipebombProjectile *>(this);

		if (!proj->m_bTouched && other->MyCombatCharacterPointer() != nullptr && other->GetTeamNumber() != proj->GetTeamNumber()) {
			int stick = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(proj->GetOriginalLauncher(), stick, stickbomb_stick_to_enemies);

			if (stick != 0) {
				proj->m_bTouched = true;
				
				auto phys = proj->VPhysicsGetObject();
				if (phys != nullptr) {
					phys->EnableMotion(false);
				}

				if (other->IsPlayer()) {
					stick_info.push_back({proj, other, proj->GetAbsOrigin() - other->GetAbsOrigin()});
				}
				else {
					proj->SetParent(other, -1);
				}
			}
			float flFizzle = 0;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(proj->GetOriginalLauncher(), flFizzle, stickybomb_fizzle_time);
			if (flFizzle > 0) {
				proj->SetDetonateTimerLength(flFizzle);
			}
			
		}
		DETOUR_MEMBER_CALL(CTFGrenadePipebombProjectile_StickybombTouch)(other);
	}
	
	
	DETOUR_DECL_MEMBER(void, CTFPlayer_DropCurrencyPack, int pack, int amount, bool forcedistribute, CTFPlayer *moneymaker )
	{
		if (killer_weapon != nullptr) {
			int distribute = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(killer_weapon, distribute, collect_currency_on_kill);
			if (distribute != 0) {
				forcedistribute = true;
				moneymaker = ToTFPlayer(killer_weapon->GetOwnerEntity());
			}
		}
		DETOUR_MEMBER_CALL(CTFPlayer_DropCurrencyPack)(pack, amount, forcedistribute, moneymaker);

	}
	
	DETOUR_DECL_MEMBER(float, CTeamplayRoundBasedRules_GetMinTimeWhenPlayerMaySpawn, CBasePlayer *player)
	{
		float respawntime = GetFastAttributeFloat(player, 0.0f, MIN_RESPAWN_TIME);

		if (!player->IsBot() && respawntime != 0.0f) {
			return player->GetDeathTime() + respawntime;
		}

		return DETOUR_MEMBER_CALL(CTeamplayRoundBasedRules_GetMinTimeWhenPlayerMaySpawn)(player);
	}

	DETOUR_DECL_MEMBER(void, CTFGameRules_OnPlayerSpawned, CTFPlayer *player)
	{
		DETOUR_MEMBER_CALL(CTFGameRules_OnPlayerSpawned)(player);
		for (size_t i = 0; i < stick_info.size(); ) {
			auto &entry = stick_info[i];
			if (entry.sticked == player && entry.sticky != nullptr) {
				stick_info.erase(stick_info.begin() + i);
				auto phys = entry.sticky->VPhysicsGetObject();
				if (phys != nullptr) {
					phys->EnableMotion(true);
				}
				continue;
			}
			i++;
		}
	}
	
	DETOUR_DECL_MEMBER(void, CTFMinigun_WindDown)
	{
		auto minigun = reinterpret_cast<CTFMinigun *>(this);
		if (minigun->GetItem() != nullptr) {
			GET_STRING_ATTRIBUTE(minigun->GetItem()->GetAttributeList(), "custom wind down sound", str);
			if (str != nullptr) {
				PrecacheSound(str);
				minigun->EmitSound(str);
			}
		}
        DETOUR_MEMBER_CALL(CTFMinigun_WindDown)();
    }
	
	DETOUR_DECL_MEMBER(void, CTFMinigun_WindUp)
	{
		auto minigun = reinterpret_cast<CTFMinigun *>(this);
		if (minigun->GetItem() != nullptr) {
			GET_STRING_ATTRIBUTE(minigun->GetItem()->GetAttributeList(), "custom wind up sound", str);
			if (str != nullptr) {
				PrecacheSound(str);
				minigun->EmitSound(str);
			}
		}
        DETOUR_MEMBER_CALL(CTFMinigun_WindUp)();
    }

	DETOUR_DECL_MEMBER(void, CTFPlayer_DropAmmoPack, const CTakeDamageInfo& info, bool b1, bool b2)
	{
		auto player = reinterpret_cast<CTFPlayer *>(this);
		for(int i = 0; i < player->WeaponCount(); i++ ) {
			CBaseCombatWeapon *weapon = player->GetWeapon(i);
			if (weapon == nullptr || weapon == player->GetActiveTFWeapon() || weapon->GetItem() == nullptr) continue;

			int droppedWeapon = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, droppedWeapon, is_dropped_weapon);
			
			if (droppedWeapon != 0) {
				auto dropped = CTFDroppedWeapon::Create(player, player->EyePosition(), vec3_angle, weapon->GetWorldModel(), weapon->GetItem());
				if (dropped != nullptr)
					dropped->InitDroppedWeapon(player, static_cast<CTFWeaponBase *>(weapon), info.GetAttacker() != nullptr && info.GetAttacker()->GetTeamNumber() == player->GetTeamNumber(), false);
			}
		}
		DETOUR_MEMBER_CALL(CTFPlayer_DropAmmoPack)(info, b1, b2);
	}

	DETOUR_DECL_STATIC(CTFDroppedWeapon *, CTFDroppedWeapon_Create, CTFPlayer *pOwner, const Vector& vecOrigin, const QAngle& vecAngles, const char *pszModelName, const CEconItemView *pItemView)
	{
		if (pItemView != nullptr) {
			CAttributeList &list = pItemView->GetAttributeList();
			GET_STRING_ATTRIBUTE(list, "custom item model", model);
			if (model != nullptr) {
				pszModelName = model;
			}
		}
		return DETOUR_STATIC_CALL(CTFDroppedWeapon_Create)(pOwner, vecOrigin, vecAngles, pszModelName, pItemView);
	}
	
	RefCount rc_CTFPlayer_Regenerate;
	DETOUR_DECL_MEMBER(void, CTFPlayer_Regenerate, bool ammo)
	{
		SCOPED_INCREMENT(rc_CTFPlayer_Regenerate);
		DETOUR_MEMBER_CALL(CTFPlayer_Regenerate)(ammo);
	}

	DETOUR_DECL_MEMBER(bool, CTFPlayer_ItemsMatch, void *pData, CEconItemView *pCurWeaponItem, CEconItemView *pNewWeaponItem, CTFWeaponBase *pWpnEntity)
	{
		bool ret = DETOUR_MEMBER_CALL(CTFPlayer_ItemsMatch)(pData, pCurWeaponItem, pNewWeaponItem, pWpnEntity);
		
		if (pCurWeaponItem != nullptr && pNewWeaponItem != nullptr) {
			DevMsg("%lld %lld %d %s %s\n", pCurWeaponItem->m_iItemID + 0LL, pNewWeaponItem->m_iItemID + 0LL, ret, GetItemNameForDisplay(pCurWeaponItem), GetItemNameForDisplay(pNewWeaponItem));
		}
		if (!ret && rc_CTFPlayer_Regenerate) {
			int stay = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(pWpnEntity, stay, stay_after_regenerate);
			if (stay != 0) {
				return true;
			} 
		}
		return ret;
	}

	THINK_FUNC_DECL(MinigunClearSounds)
	{
		auto minigun = reinterpret_cast<CTFMinigun *>(this);
		minigun->StopSound(minigun->GetShootSound(WPN_DOUBLE));
		minigun->StopSound(minigun->GetShootSound(BURST));
		minigun->StopSound(minigun->GetShootSound(SPECIAL3));
		
	}

	THINK_FUNC_DECL(FlameThrowerClearSounds)
	{
		auto flamethrower = reinterpret_cast<CTFFlameThrower *>(this);
		flamethrower->StopSound(flamethrower->GetShootSound(SINGLE));
		flamethrower->StopSound(flamethrower->GetShootSound(BURST));
		flamethrower->StopSound(flamethrower->GetShootSound(SPECIAL1));
		
	}

	DETOUR_DECL_MEMBER(void, CTFMinigun_SetWeaponState, CTFMinigun::MinigunState_t state)
	{
		auto minigun = reinterpret_cast<CTFMinigun *>(this);
		if (state != minigun->m_iWeaponState) {
			GET_STRING_ATTRIBUTE(minigun->GetItem()->GetAttributeList(), "custom weapon fire sound", soundfiring);
			GET_STRING_ATTRIBUTE(minigun->GetItem()->GetAttributeList(), "custom minigun spin sound", soundspinning);

			if (soundfiring != nullptr) {
				if (state == CTFMinigun::AC_STATE_FIRING) {
					minigun->EmitSound(soundfiring);
					THINK_FUNC_SET(minigun, MinigunClearSounds, gpGlobals->curtime);
				}
				else {
					minigun->StopSound(soundfiring);
				}
			}
			if (soundspinning != nullptr) {
				if (state == CTFMinigun::AC_STATE_SPINNING) {
					minigun->EmitSound(soundspinning);
					THINK_FUNC_SET(minigun, MinigunClearSounds, gpGlobals->curtime);
				}
				else {
					minigun->StopSound(soundspinning);
				}
			}
		}
		DETOUR_MEMBER_CALL(CTFMinigun_SetWeaponState)(state);
	}
	
	DETOUR_DECL_MEMBER(__gcc_regcall void, CTFFlameThrower_SetWeaponState, int state)
	{
		auto flamethrower = reinterpret_cast<CTFFlameThrower *>(this);
		if (state != flamethrower->m_iWeaponState) {
			GET_STRING_ATTRIBUTE(flamethrower->GetItem()->GetAttributeList(), "custom weapon fire sound", soundfiring);

			if (soundfiring != nullptr) {
				if ((state == 1 || state == 2) && flamethrower->m_iWeaponState != 1 && flamethrower->m_iWeaponState != 2) {
					flamethrower->EmitSound(soundfiring);
				}
				else if (state != 1 && state != 2) {
					flamethrower->StopSound(soundfiring);
				}
				THINK_FUNC_SET(flamethrower, FlameThrowerClearSounds, gpGlobals->curtime);
			}
		}
		DETOUR_MEMBER_CALL(CTFFlameThrower_SetWeaponState)(state);
	}

	
	DETOUR_DECL_MEMBER(void, CTFProjectile_Arrow_FadeOut, int time)
	{
		auto arrow = reinterpret_cast<CTFProjectile_Arrow *>(this);
		DETOUR_MEMBER_CALL(CTFProjectile_Arrow_FadeOut)(time);
		float remove = 0;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(arrow->GetOriginalLauncher(), remove, arrow_hit_kill_time);
		if (remove != 0) {
			arrow->SetNextThink(gpGlobals->curtime + remove, "ARROW_REMOVE_THINK");
		}

	}

	DETOUR_DECL_MEMBER(void, CTFProjectile_Arrow_CheckSkyboxImpact, CBaseEntity *pOther)
	{
		auto arrow = reinterpret_cast<CTFProjectile_Arrow *>(this);
		float bounce_speed = 0;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(arrow->GetOriginalLauncher(), bounce_speed, grenade_bounce_speed);
		if (bounce_speed != 0) {
			trace_t &tr = CBaseEntity::GetTouchTrace();
			//Vector velDir = arrow->GetAbsVelocity();
			//VectorNormalize(velDir);
			//Vector vecSpot = arrow->GetAbsOrigin() - velDir * 32;
			//UTIL_TraceLine(vecSpot, vecSpot + velDir * 64, MASK_SOLID, arrow, COLLISION_GROUP_DEBRIS, &tr);

			if (tr.DidHit()) {
				Vector pre_vel = arrow->GetAbsVelocity();
				Vector &normal = tr.plane.normal;
				Vector mirror_vel = (pre_vel - 2 * (pre_vel.Dot(normal)) * normal) * bounce_speed;
				arrow->SetAbsVelocity(mirror_vel);
				QAngle angles;
				VectorAngles(mirror_vel, angles);
				arrow->SetAbsAngles(angles);
				return;
			}
		}
		DETOUR_MEMBER_CALL(CTFProjectile_Arrow_CheckSkyboxImpact)(pOther);
	}

	DETOUR_DECL_MEMBER(int, CTFPlayerShared_CalculateObjectCost, CTFPlayer *builder, int object)
	{
		auto shared = reinterpret_cast<CTFPlayerShared *>(this);

		int result = DETOUR_MEMBER_CALL(CTFPlayerShared_CalculateObjectCost)(builder, object);

		if (object == OBJ_SENTRYGUN) {
			float sentry_cost = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(builder, sentry_cost, mod_sentry_cost);
			result *= sentry_cost;
			if (sentry_cost > 1.0f && result > builder->GetAmmoCount( TF_AMMO_METAL )) {
				gamehelpers->TextMsg(ENTINDEX(builder), TEXTMSG_DEST_CENTER, CFmtStr("You need %d metal to build a sentry gun", result));
			}
		}
		else if (object == OBJ_DISPENSER) {
			float dispenser_cost = 1.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(builder, dispenser_cost, mod_dispenser_cost);
			result *= dispenser_cost;
			if (dispenser_cost > 1.0f && result > builder->GetAmmoCount( TF_AMMO_METAL )) {
				gamehelpers->TextMsg(ENTINDEX(builder), TEXTMSG_DEST_CENTER, CFmtStr("You need %d metal to build a dispenser", result));
			}
		}
		return result;
	}

	DETOUR_DECL_MEMBER(ETFDmgCustom, CTFWeaponBase_GetPenetrateType)
	{
		auto result = DETOUR_MEMBER_CALL(CTFWeaponBase_GetPenetrateType)();

		if (result == TF_DMG_CUSTOM_NONE) {
			int penetrate = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(reinterpret_cast<CTFWeaponBase *>(this), penetrate, penetrate_teammates);
			if (penetrate != 0)
				return TF_DMG_CUSTOM_PENETRATE_MY_TEAM;
		}
		return result;
	}

	DETOUR_DECL_MEMBER(float, CBaseProjectile_GetCollideWithTeammatesDelay)
	{
		int penetrate = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(reinterpret_cast<CBaseProjectile *>(this)->GetOriginalLauncher(), penetrate, penetrate_teammates);
		if (penetrate) {
			return 9999.0f;
		}

		return DETOUR_MEMBER_CALL(CBaseProjectile_GetCollideWithTeammatesDelay)();
	}

	DETOUR_DECL_MEMBER(void, CTFPlayer_TFPlayerThink)
	{
		if (gpGlobals->tickcount % 8 == 6) {
			auto player = reinterpret_cast<CTFPlayer *>(this);
			static ConVarRef stepsize("sv_stepsize");
			player->m_Local->m_flStepSize = GetFastAttributeFloat(player, stepsize.GetFloat(), MULT_STEP_HEIGHT);
		}

		DETOUR_MEMBER_CALL(CTFPlayer_TFPlayerThink)();
	}

	DETOUR_DECL_MEMBER(unsigned int, CTFGameMovement_PlayerSolidMask, bool brushonly)
	{
		CBasePlayer *player = reinterpret_cast<CGameMovement *>(this)->player;
		unsigned int mask = DETOUR_MEMBER_CALL(CTFGameMovement_PlayerSolidMask)(brushonly);
		if (GetFastAttributeInt(player, 0, IGNORE_PLAYER_CLIP) != 0)
			mask &= ~CONTENTS_PLAYERCLIP;

		return mask;
	}

	DETOUR_DECL_MEMBER(void, CTFPlayer_PlayerRunCommand, CUserCmd* cmd, IMoveHelper* moveHelper)
	{
		CTFPlayer* player = reinterpret_cast<CTFPlayer*>(this);
		if( (cmd->buttons & 2) && (player->GetGroundEntity() == nullptr) && player->IsAlive() 
				/*&& (player->GetFlags() & 1) */ && GetFastAttributeInt(player, 0, ALLOW_BUNNY_HOP) == 1
				 ){
			cmd->buttons &= ~2;
		}
		DETOUR_MEMBER_CALL(CTFPlayer_PlayerRunCommand)(cmd, moveHelper);
	}

	DETOUR_DECL_MEMBER(void, CTFGameMovement_PreventBunnyJumping)
	{
		if(GetFastAttributeInt(reinterpret_cast<CGameMovement *>(this)->player, 0, ALLOW_BUNNY_HOP) == 0){
			DETOUR_MEMBER_CALL(CTFGameMovement_PreventBunnyJumping)();
		}
	}

	DETOUR_DECL_MEMBER(bool, CTFWeaponBase_Reload)
	{
		auto weapon = reinterpret_cast<CTFWeaponBase *>(this);
		int iWeaponMod = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, iWeaponMod, reload_full_clip_at_once );
		if (iWeaponMod == 1 && !weapon->IsEnergyWeapon())
		{
			weapon->m_bReloadsSingly = false;
		}

		return DETOUR_MEMBER_CALL(CTFWeaponBase_Reload)();
	}

	DETOUR_DECL_MEMBER(void, CTFWeaponBase_Holster)
	{
		auto weapon = reinterpret_cast<CTFWeaponBase *>(this);

		// Remove add cond on active effects
		CTFPlayer *owner = ToTFPlayer(weapon->GetOwnerEntity());
		if (owner != nullptr) {
			int alwaysCrit = GetFastAttributeInt(weapon, 0, ALWAYS_CRIT);

			if (alwaysCrit && owner->m_Shared->GetConditionDuration(TF_COND_CRITBOOSTED_CARD_EFFECT) < 0.5f) {
				owner->m_Shared->RemoveCond(TF_COND_CRITBOOSTED_CARD_EFFECT);
			}
			
			int addcond = GetFastAttributeInt(weapon, 0, ADD_COND_ON_ACTIVE);
			if (addcond != 0) {
				for (int i = 0; i < 3; i++) {
					int addcond_single = (addcond >> (i * 8)) & 255;
					if (addcond_single != 0) {
						if (alwaysCrit && owner->m_Shared->GetConditionDuration((ETFCond)addcond_single) < 0.5f) {
							owner->m_Shared->RemoveCond((ETFCond)addcond_single);
						}
					}
				}
			}
		}
		weapon->GetOrCreateEntityModule<WeaponModule>("weapon")->consecutiveShotsScore = 0.0f;
		DETOUR_MEMBER_CALL(CTFWeaponBase_Holster)();
	}

	DETOUR_DECL_MEMBER(void, CBaseProjectile_D2)
	{
		auto projectile = reinterpret_cast<CBaseProjectile *>(this);
		auto weapon = ToBaseCombatWeapon(projectile->GetOriginalLauncher());
		if (weapon != nullptr) {
			GET_STRING_ATTRIBUTE(weapon->GetItem()->GetAttributeList(), "projectile sound", sound);
			if (sound != nullptr) {
				projectile->StopSound(sound);
			}
		}
        DETOUR_MEMBER_CALL(CBaseProjectile_D2)();
    }

	std::map<CHandle<CTFWeaponBaseGun>, float> applyGunDelay;
	DETOUR_DECL_MEMBER(void, CTFWeaponBaseGun_PrimaryAttack)
	{
		auto weapon = reinterpret_cast<CTFWeaponBaseGun *>(this);
		auto mod = weapon->GetOrCreateEntityModule<WeaponModule>("weapon");

		auto oldHost = g_RecipientFilterPredictionSystem.GetRef().m_pSuppressHost;
		// Burst mode 

		//if (mod->lastAttackCooldown != 0.0f && gpGlobals->curtime < mod->lastShotTime + mod->lastAttackCooldown + gpGlobals->frametime) {
		//	g_RecipientFilterPredictionSystem.GetRef().m_pSuppressHost = nullptr;
		//}

		DETOUR_MEMBER_CALL(CTFWeaponBaseGun_PrimaryAttack)();
		if (weapon->m_flNextPrimaryAttack > gpGlobals->curtime) {
			float attackTime = weapon->m_flNextPrimaryAttack - gpGlobals->curtime;
			mod->consecutiveShotsScore += (weapon->m_flNextPrimaryAttack - gpGlobals->curtime);
			
			static int auto_fires_full_clip_id = GetItemSchema()->GetAttributeDefinitionByName("auto fires full clip")->GetIndex();
			int burst_num = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, burst_num, burst_fire_count);
			if (burst_num != 0) {
				bool enforced = burst_num < 0;
				int burst_num_real = abs(burst_num) - 1;
				mod->lastAttackCooldown = attackTime;
				if (gpGlobals->curtime > mod->lastShotTime + attackTime + gpGlobals->frametime ) {
					mod->burstShotNumber = 0;
				}
				mod->lastShotTime = gpGlobals->curtime;

				if (mod->burstShotNumber < burst_num_real) {
					if (enforced && mod->burstShotNumber == 0) {
						weapon->GetItem()->GetAttributeList().SetRuntimeAttributeValueByDefID(auto_fires_full_clip_id, 1.0f);
					}
					float ping = 0;
					auto netinfo = engine->GetPlayerNetInfo(ENTINDEX(weapon->GetTFPlayerOwner()));
					if (netinfo != nullptr) {
						ping = netinfo->GetLatency(FLOW_OUTGOING) - 0.03f;
					}
					int shotsinping = ping / attackTime;
					if (mod->burstShotNumber >= burst_num_real - shotsinping) {
						applyGunDelay[weapon] = gpGlobals->curtime + attackTime;
					} 
					mod->burstShotNumber += 1;
				}
				else {
					mod->burstShotNumber = 0;
					float burst_fire_rate = 1.0f;
					CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, burst_fire_rate, burst_fire_rate_mult);
					weapon->m_flNextPrimaryAttack = gpGlobals->curtime + attackTime * burst_fire_rate;
					if (enforced) {
						weapon->GetItem()->GetAttributeList().RemoveAttributeByDefID(auto_fires_full_clip_id);
					}
				}
				if (enforced && (weapon->m_iClip1 == 0 || weapon->m_flEnergy < weapon->Energy_GetShotCost())) {
					weapon->GetItem()->GetAttributeList().RemoveAttributeByDefID(auto_fires_full_clip_id);
				}
			}
			
			int fire_full_clip = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, fire_full_clip, force_fire_full_clip);
			if (fire_full_clip != 0) {
				if (weapon->m_iClip1 != 0 && weapon->m_flEnergy >= weapon->Energy_GetShotCost()) {
					weapon->GetItem()->GetAttributeList().SetRuntimeAttributeValueByDefID(auto_fires_full_clip_id, 1.0f);
				}
				else {
					weapon->GetItem()->GetAttributeList().RemoveAttributeByDefID(auto_fires_full_clip_id);
				}
			}
		}
		g_RecipientFilterPredictionSystem.GetRef().m_pSuppressHost = oldHost;

	}

	DETOUR_DECL_STATIC(void, SV_ComputeClientPacks, int clientCount,  void **clients, void *snapshot)
	{
		if (!applyGunDelay.empty()) {
			for (auto it = applyGunDelay.begin(); it != applyGunDelay.end();) {
				if (it->first == nullptr || it->second < gpGlobals->curtime) {
					it = applyGunDelay.erase(it);
					continue;
				}
				it->first->GetOrCreateEntityModule<WeaponModule>("weapon")->attackTimeSave = it->first->m_flNextPrimaryAttack;
				it->first->m_flNextPrimaryAttack = gpGlobals->curtime + 1.0f;
				it++;
			}
		}
		DETOUR_STATIC_CALL(SV_ComputeClientPacks)(clientCount, clients, snapshot);
		if (!applyGunDelay.empty()) {
			for (auto it = applyGunDelay.begin(); it != applyGunDelay.end(); it++) {
				it->first->m_flNextPrimaryAttack = it->first->GetOrCreateEntityModule<WeaponModule>("weapon")->attackTimeSave;
			}
		} 
	}

	DETOUR_DECL_MEMBER(void, CTFPlayer_Spawn)
	{
		CTFPlayer *player = reinterpret_cast<CTFPlayer *>(this); 
		for (int i = 0; i < MAX_WEAPONS; i++) {
			auto weapon = player->GetWeapon(i);
			if (weapon != nullptr) {
				int fire_full_clip = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, fire_full_clip, force_fire_full_clip);
				int burst_num = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, burst_num, burst_fire_count);
				int auto_full_clip = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, auto_full_clip, auto_fires_full_clip);
				if ((burst_num < 0 || fire_full_clip != 0) && auto_full_clip != 0) {
					static int auto_fires_full_clip_id = GetItemSchema()->GetAttributeDefinitionByName("auto fires full clip")->GetIndex();
					weapon->GetItem()->GetAttributeList().RemoveAttributeByDefID(auto_fires_full_clip_id);
				}
			}
		}
		
		DETOUR_MEMBER_CALL(CTFPlayer_Spawn)();
	}

	DETOUR_DECL_MEMBER(QAngle, CTFWeaponBase_GetSpreadAngles)
	{
		auto ret = DETOUR_MEMBER_CALL(CTFWeaponBase_GetSpreadAngles)();
		auto weapon = reinterpret_cast<CTFWeaponBase *>(this);
		auto eye = weapon->GetTFPlayerOwner()->EyeAngles();
		if (eye != ret) {
			auto diff = ret - eye;
			auto mod = weapon->GetEntityModule<WeaponModule>("weapon");
			if (mod != nullptr && mod->totalAccuracyApplied != 1.0f) {
				diff *= mod->totalAccuracyApplied;
			}
			ret = eye + diff;
		}
		return ret;
	}

	inline int IsCustomAttribute(CEconItemAttribute &attr) {
		return (attr.GetAttributeDefinitionIndex() > 4200 && attr.GetAttributeDefinitionIndex() < 5000);
	}

	DETOUR_DECL_MEMBER(void, CAttributeList_SetRuntimeAttributeValue, const CEconItemAttributeDefinition *pAttrDef, float flValue)
	{	
		auto list = reinterpret_cast<CAttributeList *>(this);
		auto &attrs = list->Attributes();
		int countpre = attrs.Count();
		DETOUR_MEMBER_CALL(CAttributeList_SetRuntimeAttributeValue)(pAttrDef, flValue);
		// Move around attributes so that the custom attributes appear at the end of the list
		if (pAttrDef != nullptr && countpre != attrs.Count() && attrs.Count() > 20 && (pAttrDef->GetIndex() < 4200 || pAttrDef->GetIndex() > 5000)) {
			int count = attrs.Count();
			int i = 1;
			while (i < count) {
				auto x = attrs[i];
				int cmp = IsCustomAttribute(x);
				int j = i - 1;
				while (j >= 0 && IsCustomAttribute(attrs[j]) > cmp) {
						attrs[j+1] = attrs[j];
					j = j - 1;
				}
				attrs[j+1] = x;
				i = i + 1;
			}
			/*std::sort(attrs.begin(), attrs.end(), [](auto a, auto b) {
				return IsCustomAttribute(a) - IsCustomAttribute(b);
			}); */
			list->NotifyManagerOfAttributeValueChanges();
		}
	}

	DETOUR_DECL_MEMBER(float, CTFKnife_GetMeleeDamage, CBaseEntity *pTarget, int* piDamageType, int* piCustomDamage)
	{
		float ret = DETOUR_MEMBER_CALL(CTFKnife_GetMeleeDamage)(pTarget, piDamageType, piCustomDamage);
		auto knife = reinterpret_cast<CTFKnife *>(this);

		if (*piCustomDamage == TF_DMG_CUSTOM_BACKSTAB && !knife->GetTFPlayerOwner()->IsBot() && pTarget->IsPlayer() && ToTFPlayer(pTarget)->IsMiniBoss() ) {
			
			//int backstabs = backstab_count[pTarget];

			float armor_piercing_attr = 25.0f;
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( knife, armor_piercing_attr, armor_piercing );
			if (armor_piercing_attr > 125 || armor_piercing_attr < 25) {
				ret = 250 * (armor_piercing_attr / 100.0f);
			}
		}
		return ret;
	}

	void RemoveMedigunAttributes(CTFPlayer *target, const char *attribs)
	{
		std::string str(attribs);
		boost::tokenizer<boost::char_separator<char>> tokens(str, boost::char_separator<char>("|"));

		auto it = tokens.begin();
		while (it != tokens.end()) {
			auto attribute = *it;
			if (++it == tokens.end())
				break;

			auto attr_def = GetItemSchema()->GetAttributeDefinitionByName(attribute.c_str());
			if (attr_def != nullptr) {
				target->GetAttributeList()->RemoveAttribute(attr_def);
				target->TeamFortress_SetSpeed();
			}
			++it;
		}
	}

	DETOUR_DECL_MEMBER(void, CWeaponMedigun_RemoveHealingTarget, bool flag)
	{
		auto medigun = reinterpret_cast<CWeaponMedigun *>(this);
		auto target = ToTFPlayer(medigun->GetHealTarget());
		if (target != nullptr) {
			GET_STRING_ATTRIBUTE(medigun->GetItem()->GetAttributeList(), "medigun passive attributes", attribs);
			if (attribs != nullptr) {
				RemoveMedigunAttributes(target, attribs);
			}
			GET_STRING_ATTRIBUTE(medigun->GetItem()->GetAttributeList(), "medigun passive attributes owner", attribsOwner);
			if (attribsOwner != nullptr && medigun->GetTFPlayerOwner() != nullptr) {
				RemoveMedigunAttributes(medigun->GetTFPlayerOwner(), attribsOwner);
			}
		}
		
        DETOUR_MEMBER_CALL(CWeaponMedigun_RemoveHealingTarget)(flag);

    }
	void AddMedigunAttributes(CTFPlayer *target, const char *attribs)
	{
		std::string str(attribs);
		boost::tokenizer<boost::char_separator<char>> tokens(str, boost::char_separator<char>("|"));

		auto it = tokens.begin();
		while (it != tokens.end()) {
			auto attribute = *it;
			if (++it == tokens.end())
				break;
			auto &value = *it;
			auto attr_def = GetItemSchema()->GetAttributeDefinitionByName(attribute.c_str());
			if (attr_def != nullptr) {
				target->GetAttributeList()->SetRuntimeAttributeValue(attr_def, strtof(value.c_str(), nullptr));
				target->TeamFortress_SetSpeed();
			}
			++it;
		}
	}
	DETOUR_DECL_MEMBER(void, CWeaponMedigun_StartHealingTarget, CBaseEntity *targete)
	{
		auto medigun = reinterpret_cast<CWeaponMedigun *>(this);
		
		auto target = ToTFPlayer(targete);
		if (target != nullptr) {
			GET_STRING_ATTRIBUTE(medigun->GetItem()->GetAttributeList(), "medigun passive attributes", attribs);
			if (attribs != nullptr) {
				AddMedigunAttributes(target, attribs);
			}
			GET_STRING_ATTRIBUTE(medigun->GetItem()->GetAttributeList(), "medigun passive attributes owner", attribsOwner);
			if (attribsOwner != nullptr && medigun->GetTFPlayerOwner() != nullptr) {
				AddMedigunAttributes(medigun->GetTFPlayerOwner(), attribsOwner);
			}
		}
		
        DETOUR_MEMBER_CALL(CWeaponMedigun_StartHealingTarget)(targete);

    }

	DETOUR_DECL_MEMBER(bool, CTFWeaponBase_Energy_Recharge)
	{
		auto weapon = reinterpret_cast<CTFWeaponBase *>(this);
		
		int iWeaponMod = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER(weapon, iWeaponMod, reload_full_clip_at_once );
		if (iWeaponMod == 1)
		{
			while(true) {
				if (DETOUR_MEMBER_CALL(CTFWeaponBase_Energy_Recharge)()) {
					break;
				}
			}
			//weapon->m_flEnergy = weapon->Energy_GetMaxEnergy();
			//return true;
		}

		return DETOUR_MEMBER_CALL(CTFWeaponBase_Energy_Recharge)();
	}

	DETOUR_DECL_MEMBER(void, CBasePlayer_ItemPostFrame)
	{
		auto player = reinterpret_cast<CTFPlayer *>(this);
		auto weapon = player->GetActiveTFWeapon();
		bool pressedM2 = false;
		if (weapon != nullptr) {
			if (GetFastAttributeInt(weapon, 0, ALT_FIRE_DISABLED) != 0) {
				weapon->m_flNextSecondaryAttack = gpGlobals->curtime + 1.0f;
				if (player->m_nButtons & IN_ATTACK2) {
					player->m_nButtons &= ~IN_ATTACK2;
					pressedM2 = true;
				}
			}
		}

		DETOUR_MEMBER_CALL(CBasePlayer_ItemPostFrame)();

		if (pressedM2) {
			player->m_nButtons |= IN_ATTACK2;
		}
	}

	DETOUR_DECL_MEMBER(float, CWeaponMedigun_GetHealRate)
	{
		auto weapon = reinterpret_cast<CWeaponMedigun *>(this);

		auto healRate = DETOUR_MEMBER_CALL(CWeaponMedigun_GetHealRate)();
		if (rtti_cast<CTFReviveMarker *>(weapon->GetHealTarget()) != nullptr) {
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER(weapon, healRate, revive_rate);
		}
		return healRate;
	}

	ConVar cvar_display_attrs("sig_attr_display", "1", FCVAR_NONE,	
		"Enable displaying custom attributes on the right side of the screen");	

	std::vector<std::string> attribute_info_strings[33];
	float attribute_info_display_time[33];

	void DisplayAttributeString(CTFPlayer *player, int num)
	{
		auto &vec = attribute_info_strings[ENTINDEX(player) - 1];

		CRecipientFilter filter;
		filter.AddRecipient(player);
		bf_write *msg = engine->UserMessageBegin(&filter, usermessages->LookupUserMessage("KeyHintText"));
		msg->WriteByte(1);

		if (num < (int) vec.size()) {
			std::string &str = vec[num];
			msg->WriteString(str.c_str());
		}
		else {
			msg->WriteString("");
			vec.clear();
		}

		engine->MessageEnd();
	}

	void ClearAttributeDisplay(CTFPlayer *player)
	{
		CRecipientFilter filter;
		filter.AddRecipient(player);
		bf_write *msg = engine->UserMessageBegin(&filter, usermessages->LookupUserMessage("KeyHintText"));
		msg->WriteByte(1);
		msg->WriteString("");
		engine->MessageEnd();
	}

	THINK_FUNC_DECL(DisplayAttributeString1) {
		DisplayAttributeString(reinterpret_cast<CTFPlayer *>(this), 1);
	}
	THINK_FUNC_DECL(DisplayAttributeString2) {
		DisplayAttributeString(reinterpret_cast<CTFPlayer *>(this), 2);
	}
	THINK_FUNC_DECL(DisplayAttributeString3) {
		DisplayAttributeString(reinterpret_cast<CTFPlayer *>(this), 3);
	}
	THINK_FUNC_DECL(DisplayAttributeString4) {
		DisplayAttributeString(reinterpret_cast<CTFPlayer *>(this), 4);
	}
	THINK_FUNC_DECL(DisplayAttributeString5) {
		DisplayAttributeString(reinterpret_cast<CTFPlayer *>(this), 5);
	}
	THINK_FUNC_DECL(DisplayAttributeString6) {
		DisplayAttributeString(reinterpret_cast<CTFPlayer *>(this), 6);
	}

	void DisplayAttributes(int &indexstr, std::vector<std::string> &attribute_info_vec, CUtlVector<CEconItemAttribute> &attrs, CTFPlayer *player, CEconItemView *item_def, bool display_stock)
	{
		bool added_item_name = false;
		int slot = 0;//reinterpret_cast<CTFItemDefinition *>(GetItemSchema()->GetItemDefinition(item_def))->GetLoadoutSlot(player->GetPlayerClass()->GetClassIndex());
		if (display_stock && (item_def == nullptr || slot < LOADOUT_POSITION_PDA2 || slot == LOADOUT_POSITION_ACTION) ) {
			added_item_name = true;
			if (item_def != nullptr)
				attribute_info_vec.back() += CFmtStr("\n%s:\n\n", GetItemNameForDisplay(item_def));
			else
				attribute_info_vec.back() += "\nCharacter Attributes:\n\n";
		}
		for (int i = 0; i < attrs.Count(); i++) {
			CEconItemAttribute &attr = attrs[i];
			CEconItemAttributeDefinition *attr_def = attr.GetStaticData();
			
			if ((!display_stock && attr_def->GetIndex() < 4000) || attr_def == nullptr)
				continue;

			std::string format_str;
			if (!FormatAttributeString(format_str, attr.GetStaticData(), *attr.GetValuePtr()))
				continue;

			// break lines
			int space_pos = 0;
			int last_space_pos = 0;
			int find_newline_pos = 0;
			while((unsigned) space_pos < format_str.size()) {
				space_pos = format_str.find(" ", space_pos);
				if (space_pos == -1) {
					space_pos = format_str.size();
				}
				if (space_pos - find_newline_pos > 28 /*25*/) {
					format_str.insert(last_space_pos - 1, "\n");
					space_pos++;
					find_newline_pos = last_space_pos;
				}
				space_pos++;
				last_space_pos = space_pos;
			}

			// replace \n with newline
			int newline_pos;
			while((newline_pos = format_str.find("\\n")) != -1) {
				format_str.replace(newline_pos, 2, "\n");
			} 

			// Replace percent symbols as HintKeyText parses them as keys
			int percent_pos;
			while((percent_pos = format_str.find("%")) != -1) {
				format_str.replace(percent_pos, 1, "℅");
			} 

			if (!added_item_name) {
				if (item_def != nullptr)
					format_str.insert(0, CFmtStr("\n%s:\n\n", GetItemNameForDisplay(item_def)));
				else
					format_str.insert(0, "\nCharacter Attributes:\n\n");

				added_item_name = true;
			}

			if (attribute_info_vec.back().size() + format_str.size() + 1 > 252 /*220*/) {
				++indexstr;
				attribute_info_vec.push_back("");
			}
			if (indexstr > 5)
				break;

			attribute_info_vec.back() += format_str + '\n';
		}
	}

	void InspectAttributes(CTFPlayer *target, CTFPlayer *player, bool force, int slot)
	{
		if (!cvar_display_attrs.GetBool())
			return;
			
		bool display_stock = player != target;

		CTFWeaponBase *weapon = target->GetActiveTFWeapon();
		CEconItemView *view = nullptr;
		if (weapon != nullptr)
			view = weapon->GetItem();

		if (slot >= 0)
			view = CTFPlayerSharedUtils::GetEconItemViewByLoadoutSlot(target, slot);

		auto &attribute_info_vec = attribute_info_strings[ENTINDEX(player) - 1];

		ClearAttributeDisplay(player);

		if (!force && !attribute_info_vec.empty()) {
			attribute_info_vec.clear();
			return;
		}

		attribute_info_vec.clear();

		attribute_info_display_time[ENTINDEX(player) - 1] = gpGlobals->curtime;

		attribute_info_vec.push_back("");

		if (display_stock) {
			attribute_info_vec.back() += "Inspecting " ;
			attribute_info_vec.back() += target->GetPlayerName();
			attribute_info_vec.back() += "\n";
		}
		int indexstr = 0;

		bool display_stock_item = display_stock || (view != nullptr && view->GetStaticData()->GetLoadoutSlot(target->GetPlayerClass()->GetClassIndex()) == -1);
		if (slot == -1) {
			DisplayAttributes(indexstr, attribute_info_vec, target->GetAttributeList()->Attributes(), target, nullptr, display_stock);
			
			if (view != nullptr)
				DisplayAttributes(indexstr, attribute_info_vec, view->GetAttributeList().Attributes(), target, view, display_stock_item);
		}
		else {
			if (view != nullptr)
				DisplayAttributes(indexstr, attribute_info_vec, view->GetAttributeList().Attributes(), target, view, display_stock_item);

			DisplayAttributes(indexstr, attribute_info_vec, target->GetAttributeList()->Attributes(), target, nullptr, display_stock);
		}

		ForEachTFPlayerEconEntity(target, [&](CEconEntity *entity){
			if (entity->GetItem() != nullptr && entity->GetItem() != view && entity->GetItem()->GetStaticData()->m_iItemDefIndex != 0) {
				DisplayAttributes(indexstr, attribute_info_vec, entity->GetItem()->GetAttributeList().Attributes(), target, entity->GetItem(), display_stock || entity->GetItem()->GetStaticData()->GetLoadoutSlot(target->GetPlayerClass()->GetClassIndex()) == -1);
			}
		});
		
		/*hudtextparms_t textparms;
		textparms.channel = 2;
		textparms.x = 1.0f;
		textparms.y = 0.0f;
		textparms.effect = 0;
		textparms.r1 = 255;
		textparms.r2 = 255;
		textparms.b1 = 255;
		textparms.b2 = 255;
		textparms.g1 = 255;
		textparms.g2 = 255;
		textparms.a1 = 0;
		textparms.a2 = 0; 
		textparms.fadeinTime = 0.f;
		textparms.fadeoutTime = 0.f;
		textparms.holdTime = 4.0f;
		textparms.fxTime = 1.0f;
		UTIL_HudMessage(player, textparms, attribute_info_vec[0].c_str());

		if (attribute_info_vec.size() > 1) {
			textparms.channel = 3;
			textparms.y = 0.45f;
			UTIL_HudMessage(player, textparms, attribute_info_vec[1].c_str());
		}*/
	
		if (!attribute_info_vec.back().empty()) {
			DisplayAttributeString(player, 0);
			THINK_FUNC_SET(player, DisplayAttributeString1, gpGlobals->curtime + 5.0f);
			THINK_FUNC_SET(player, DisplayAttributeString2, gpGlobals->curtime + 10.0f);
			THINK_FUNC_SET(player, DisplayAttributeString3, gpGlobals->curtime + 15.0f);
			THINK_FUNC_SET(player, DisplayAttributeString4, gpGlobals->curtime + 20.0f);
			THINK_FUNC_SET(player, DisplayAttributeString5, gpGlobals->curtime + 25.0f);
			THINK_FUNC_SET(player, DisplayAttributeString6, gpGlobals->curtime + 30.0f);
		}
	}

	THINK_FUNC_DECL(HideInvalidTarget)
	{
		gamehelpers->TextMsg(ENTINDEX(this), TEXTMSG_DEST_CENTER, " ");
	}
	DETOUR_DECL_MEMBER(void, CTFPlayer_InspectButtonPressed)
	{
		CTFPlayer *player = reinterpret_cast<CTFPlayer *>(this);

		Vector forward;
		AngleVectors(player->EyeAngles(), &forward);

		trace_t result;
		UTIL_TraceLine(player->EyePosition(), player->EyePosition() + 4000.0f * forward, MASK_SOLID, player, COLLISION_GROUP_NONE, &result);

		CTFPlayer *target = ToTFPlayer(result.m_pEnt);
		if (target == nullptr || target->GetTeamNumber() != player->GetTeamNumber()) {
			target = player;
		}
		else {
			THINK_FUNC_SET(player, HideInvalidTarget, gpGlobals->curtime + 0.05f);
		}
		InspectAttributes(target, player, false);

		DETOUR_MEMBER_CALL(CTFPlayer_InspectButtonPressed)();

	}

    void RemoveAttributeManager(CBaseEntity *entity) {
        
        int index = ENTINDEX(entity);
        if (entity == last_fast_attrib_entity) {
            last_fast_attrib_entity = nullptr;
        }
        delete fast_attribute_cache[index];
        fast_attribute_cache[index] = nullptr;
    }

	DETOUR_DECL_MEMBER(void, CTFPlayer_UpdateOnRemove)
	{
		int id = ENTINDEX(reinterpret_cast<CTFPlayer *>(this)) - 1;

		if (id < 0)
			return;

		attribute_info_strings[id].clear();
		attribute_info_display_time[id] = 0.0f;
        DETOUR_MEMBER_CALL(CTFPlayer_UpdateOnRemove)();
        RemoveAttributeManager(reinterpret_cast<CBaseEntity *>(this));
    }

	DETOUR_DECL_MEMBER(void, CAttributeManager_ClearCache)
	{
        DETOUR_MEMBER_CALL(CAttributeManager_ClearCache)();
        auto mgr = reinterpret_cast<CAttributeManager *>(this);

        if (mgr->m_hOuter != nullptr) {
			auto cache = fast_attribute_cache[ENTINDEX(mgr->m_hOuter)];
            if (cache != nullptr) {
				int count = mgr->m_hOuter->IsPlayer() ? ATTRIB_COUNT_PLAYER : ATTRIB_COUNT_ITEM;
				for(int i = 0; i < count; i++) {
					cache[i] = FLT_MIN;
				}
            }
        }
	}

    DETOUR_DECL_MEMBER(void, CEconEntity_UpdateOnRemove)
	{
        DETOUR_MEMBER_CALL(CEconEntity_UpdateOnRemove)();
        RemoveAttributeManager(reinterpret_cast<CBaseEntity *>(this));
    }
	
	/*void OnAttributesChange(CAttributeManager *mgr)
	{
		CBaseEntity *outer = mgr->m_hOuter;

		if (outer != nullptr) {
			CTFPlayer *owner = ToTFPlayer(outer->IsPlayer() ? outer : outer->GetOwnerEntity());
			CAttributeManager ownermgr = outer->IsPlayer() ? mgr : (owner != nullptr ? owner->GetAttributeManager() : nullptr)
			if (ownermgr != nullptr)
			{
				float gravity = ownermgr->ApplyAttributeFloatWrapper(1.0f, owner, AllocPooledString_StaticConstantStringPointer("player_gravity"));
				if (gravity != 1.0f)
					outer->SetGravity(gravity);
				DevMsg("gravity %f\n", gravity);
			}
		}
	}

	DETOUR_DECL_MEMBER(void, CAttributeManager_OnAttributeValuesChanged)
	{
        DETOUR_MEMBER_CALL(CAttributeManager_OnAttributeValuesChanged)();
        auto mgr = reinterpret_cast<CAttributeManager *>(this);
		OnAttributesChange(mgr);
	}
	DETOUR_DECL_MEMBER(void, CAttributeContainer_OnAttributeValuesChanged)
	{
        DETOUR_MEMBER_CALL(CAttributeContainer_OnAttributeValuesChanged)();
        auto mgr = reinterpret_cast<CAttributeManager *>(this);
		OnAttributesChange(mgr);
	}
	*/
	DETOUR_DECL_MEMBER(void, CAttributeContainerPlayer_OnAttributeValuesChanged)
	{
        DETOUR_MEMBER_CALL(CAttributeContainerPlayer_OnAttributeValuesChanged)();
        auto mgr = reinterpret_cast<CAttributeManager *>(this);
		auto player = ToTFPlayer(mgr->m_hOuter);
		if (player != nullptr) {
			/*auto &rec = mgr->m_Providers.Get();
			FOR_EACH_VEC(rec, i) {
				//Msg("Receiver %s\n", ent->GetClassname());
				auto econ = reinterpret_cast<CEconEntity *>(rec[i].Get());
				econ->GetAttributeContainer()->ClearCache();
			}*/
			ForEachTFPlayerEconEntity(player, [&](CEconEntity *entity){
				entity->GetAttributeContainer()->ClearCache();
			});

		}
	}

	class CMod : public IMod, public IModCallbackListener, public IFrameUpdatePostEntityThinkListener
	{
	public:
		CMod() : IMod("Attr:Custom_Attributes")
		{
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_CanAirDash, "CTFPlayer::CanAirDash");
			MOD_ADD_DETOUR_MEMBER(CWeaponMedigun_AllowedToHealTarget, "CWeaponMedigun::AllowedToHealTarget");
			MOD_ADD_DETOUR_MEMBER(CWeaponMedigun_HealTargetThink, "CWeaponMedigun::HealTargetThink");
			MOD_ADD_DETOUR_MEMBER(CTFCompoundBow_LaunchGrenade, "CTFCompoundBow::LaunchGrenade");
			MOD_ADD_DETOUR_MEMBER_PRIORITY(CTFWeaponBaseGun_FireProjectile, "CTFWeaponBaseGun::FireProjectile", HIGHEST);
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBaseGun_RemoveProjectileAmmo, "CTFWeaponBaseGun::RemoveProjectileAmmo");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBaseGun_UpdatePunchAngles, "CTFWeaponBaseGun::UpdatePunchAngles");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBaseMelee_Swing, "CTFWeaponBaseMelee::Swing");
			MOD_ADD_DETOUR_MEMBER(CBaseObject_OnTakeDamage, "CBaseObject::OnTakeDamage");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_Event_Killed, "CTFPlayer::Event_Killed");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_CreateRagdollEntity, "CTFPlayer::CreateRagdollEntity [args]");
			MOD_ADD_DETOUR_MEMBER(CTFStickBomb_Smack, "CTFStickBomb::Smack");
			
			MOD_ADD_DETOUR_MEMBER(CTFGameRules_RadiusDamage, "CTFGameRules::RadiusDamage");
			MOD_ADD_DETOUR_MEMBER(CTFGameMovement_CheckJumpButton, "CTFGameMovement::CheckJumpButton");
			MOD_ADD_DETOUR_MEMBER(CTFGameMovement_ProcessMovement, "CTFGameMovement::ProcessMovement");
			MOD_ADD_DETOUR_MEMBER(CEconEntity_UpdateModelToClass, "CEconEntity::UpdateModelToClass");
			//MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_GetShootSound, "CTFWeaponBase::GetShootSound");
			MOD_ADD_DETOUR_MEMBER(CBaseEntity_DispatchTraceAttack,    "CBaseEntity::DispatchTraceAttack");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_FireBullet,    "CTFPlayer::FireBullet");
			MOD_ADD_DETOUR_STATIC(TE_TFExplosion,    "TE_TFExplosion");
			MOD_ADD_DETOUR_MEMBER(CTFCompoundBow_GetProjectileSpeed,    "CTFCompoundBow::GetProjectileSpeed");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBaseGun_FireEnergyBall,    "CTFWeaponBaseGun::FireEnergyBall");
			MOD_ADD_DETOUR_MEMBER(CTFCrossbow_GetProjectileSpeed,    "CTFCrossbow::GetProjectileSpeed");
			MOD_ADD_DETOUR_MEMBER(CTFGrapplingHook_GetProjectileSpeed,    "CTFGrapplingHook::GetProjectileSpeed");
			MOD_ADD_DETOUR_MEMBER(CTFShotgunBuildingRescue_GetProjectileSpeed,    "CTFShotgunBuildingRescue::GetProjectileSpeed");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBaseGrenadeProj_Explode,    "CTFWeaponBaseGrenadeProj::Explode");
			MOD_ADD_DETOUR_MEMBER(CTFBaseRocket_Explode,    "CTFBaseRocket::Explode");

			MOD_ADD_DETOUR_MEMBER(CRecipientFilter_IgnorePredictionCull,    "CRecipientFilter::IgnorePredictionCull");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_GetDamageType,    "CTFWeaponBase::GetDamageType");
			MOD_ADD_DETOUR_MEMBER(CTFMinigun_CanHolster,    "CTFMinigun::CanHolster");
			MOD_ADD_DETOUR_MEMBER(CObjectSapper_ApplyRoboSapperEffects,    "CObjectSapper::ApplyRoboSapperEffects");
			MOD_ADD_DETOUR_MEMBER(CObjectSapper_IsParentValid,    "CObjectSapper::IsParentValid");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_IsAllowedToTaunt,    "CTFPlayer::IsAllowedToTaunt");
			MOD_ADD_DETOUR_MEMBER(CUpgrades_UpgradeTouch, "CUpgrades::UpgradeTouch");
			MOD_ADD_DETOUR_MEMBER(CUpgrades_EndTouch, "CUpgrades::EndTouch");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_ItemPostFrame, "CTFWeaponBase::ItemPostFrame");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_ItemBusyFrame, "CTFWeaponBase::ItemBusyFrame");
			MOD_ADD_DETOUR_MEMBER(CObjectSentrygun_FireRocket, "CObjectSentrygun::FireRocket");
			MOD_ADD_DETOUR_MEMBER(CBaseObject_StartUpgrading, "CBaseObject::StartUpgrading");
			MOD_ADD_DETOUR_MEMBER(CBaseObject_CanBeUpgraded, "CBaseObject::CanBeUpgraded");
			MOD_ADD_DETOUR_MEMBER(CObjectSentrygun_SentryThink, "CObjectSentrygun::SentryThink");
			MOD_ADD_DETOUR_MEMBER(CBaseObject_StartBuilding, "CBaseObject::StartBuilding");
			MOD_ADD_DETOUR_MEMBER(CBaseObject_StartPlacement, "CBaseObject::StartPlacement");
			MOD_ADD_DETOUR_MEMBER(CObjectTeleporter_RecieveTeleportingPlayer, "CObjectTeleporter::RecieveTeleportingPlayer");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_WantsLagCompensationOnEntity, "CTFPlayer::WantsLagCompensationOnEntity");
			MOD_ADD_DETOUR_MEMBER(CTFGameRules_FPlayerCanTakeDamage, "CTFGameRules::FPlayerCanTakeDamage");
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_StunPlayer, "CTFPlayerShared::StunPlayer");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_HealingBolt_CanHeadshot, "CTFProjectile_HealingBolt::CanHeadshot");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_GrapplingHook_CanHeadshot, "CTFProjectile_GrapplingHook::CanHeadshot");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_EnergyBall_CanHeadshot, "CTFProjectile_EnergyBall::CanHeadshot");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_EnergyRing_CanHeadshot, "CTFProjectile_EnergyRing::CanHeadshot");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Arrow_CanHeadshot, "CTFProjectile_Arrow::CanHeadshot");
			MOD_ADD_DETOUR_MEMBER(CBaseCombatCharacter_OnTakeDamage, "CBaseCombatCharacter::OnTakeDamage");
			MOD_ADD_DETOUR_MEMBER(CTFGameRules_ApplyOnDamageModifyRules, "CTFGameRules::ApplyOnDamageModifyRules");
			MOD_ADD_DETOUR_MEMBER(CBaseCombatWeapon_Equip, "CBaseCombatWeapon::Equip");
			MOD_ADD_DETOUR_MEMBER(CTFGrenadePipebombProjectile_VPhysicsCollision, "CTFGrenadePipebombProjectile::VPhysicsCollision");
			MOD_ADD_DETOUR_MEMBER(CTFGrenadePipebombProjectile_PipebombTouch, "CTFGrenadePipebombProjectile::PipebombTouch");
			MOD_ADD_DETOUR_STATIC(PropDynamic_CollidesWithGrenades, "PropDynamic_CollidesWithGrenades");
			MOD_ADD_DETOUR_MEMBER(CTraceFilterObject_ShouldHitEntity, "CTraceFilterObject::ShouldHitEntity");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_RegenThink, "CTFPlayer::RegenThink");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_CancelTaunt, "CTFPlayer::CancelTaunt");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_IsAllowedToInitiateTauntWithPartner, "CTFPlayer::IsAllowedToInitiateTauntWithPartner");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_DeflectEntity, "CTFWeaponBase::DeflectEntity");
			MOD_ADD_DETOUR_MEMBER(CTFGameRules_GetKillingWeaponName, "CTFGameRules::GetKillingWeaponName");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_Touch, "CTFPlayer::Touch");
			MOD_ADD_DETOUR_MEMBER(CUpgrades_PlayerPurchasingUpgrade, "CUpgrades::PlayerPurchasingUpgrade");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_ReapplyItemUpgrades, "CTFPlayer::ReapplyItemUpgrades");
			MOD_ADD_DETOUR_MEMBER(CUpgrades_ApplyUpgradeToItem, "CUpgrades::ApplyUpgradeToItem");
			MOD_ADD_DETOUR_MEMBER(CBaseCombatWeapon_WeaponSound, "CBaseCombatWeapon::WeaponSound");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Rocket_IsDeflectable, "CTFProjectile_Rocket::IsDeflectable");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Arrow_IsDeflectable, "CTFProjectile_Arrow::IsDeflectable");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Flare_IsDeflectable, "CTFProjectile_Flare::IsDeflectable");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_EnergyBall_IsDeflectable, "CTFProjectile_EnergyBall::IsDeflectable");
			MOD_ADD_DETOUR_MEMBER(CTFGrenadePipebombProjectile_IsDeflectable, "CTFGrenadePipebombProjectile::IsDeflectable");
			MOD_ADD_DETOUR_MEMBER(CBaseEntity_InSameTeam, "CBaseEntity::InSameTeam");
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_OnAddBalloonHead, "CTFPlayerShared::OnAddBalloonHead");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_ApplyOnHitAttributes,          "CTFWeaponBase::ApplyOnHitAttributes");
			MOD_ADD_DETOUR_MEMBER(CObjectTeleporter_TeleporterTouch,          "CObjectTeleporter::TeleporterTouch");
			MOD_ADD_DETOUR_MEMBER(CWeaponMedigun_GetTargetRange,          "CWeaponMedigun::GetTargetRange");
			MOD_ADD_DETOUR_MEMBER(CTFBotTacticalMonitor_ShouldOpportunisticallyTeleport, "CTFBotTacticalMonitor::ShouldOpportunisticallyTeleport");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Arrow_ArrowTouch, "CTFProjectile_Arrow::ArrowTouch");
			MOD_ADD_DETOUR_MEMBER(CTFRadiusDamageInfo_ApplyToEntity, "CTFRadiusDamageInfo::ApplyToEntity");
			MOD_ADD_DETOUR_MEMBER(CTFFlameManager_BCanBurnEntityThisFrame,        "CTFFlameManager::BCanBurnEntityThisFrame");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_BallOfFire_RocketTouch, "CTFProjectile_BallOfFire::RocketTouch");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_EnergyRing_ProjectileTouch, "CTFProjectile_EnergyRing::ProjectileTouch");

			MOD_ADD_DETOUR_MEMBER_PRIORITY(CTFPlayerShared_AddCond, "CTFPlayerShared::AddCond", HIGHEST);
			MOD_ADD_DETOUR_MEMBER_PRIORITY(CTFPlayerShared_RemoveCond, "CTFPlayerShared::RemoveCond", HIGHEST);
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_PulseRageBuff, "CTFPlayerShared::PulseRageBuff");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_DoTauntAttack, "CTFPlayer::DoTauntAttack");
			MOD_ADD_DETOUR_MEMBER(CTFSodaPopper_SecondaryAttack, "CTFSodaPopper::SecondaryAttack");
			MOD_ADD_DETOUR_STATIC(JarExplode, "JarExplode");
			MOD_ADD_DETOUR_MEMBER(CTFGasManager_OnCollide, "CTFGasManager::OnCollide");
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_SetChargeEffect, "CTFPlayerShared::SetChargeEffect");
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_PulseMedicRadiusHeal, "CTFPlayerShared::PulseMedicRadiusHeal");
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_SetRevengeCrits, "CTFPlayerShared::SetRevengeCrits");
			MOD_ADD_DETOUR_MEMBER(CTFFlareGun_Revenge_Deploy , "CTFFlareGun_Revenge::Deploy");
			MOD_ADD_DETOUR_MEMBER(CTFShotgun_Revenge_Deploy, "CTFShotgun_Revenge::Deploy");
			MOD_ADD_DETOUR_MEMBER(CTFRevolver_Deploy, "CTFRevolver::Deploy");
			MOD_ADD_DETOUR_MEMBER(CTFFlareGun_Revenge_Holster, "CTFFlareGun_Revenge::Holster");
			MOD_ADD_DETOUR_MEMBER(CTFShotgun_Revenge_Holster, "CTFShotgun_Revenge::Holster");
			MOD_ADD_DETOUR_MEMBER(CTFRevolver_Holster ,"CTFRevolver::Holster");
			MOD_ADD_DETOUR_MEMBER(CTFChargedSMG_SecondaryAttack ,"CTFChargedSMG::SecondaryAttack");

			MOD_ADD_DETOUR_MEMBER(CTFPlayer_OnKilledOther_Effects ,"CTFPlayer::OnKilledOther_Effects");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_TeamFortress_CalculateMaxSpeed ,"CTFPlayer::TeamFortress_CalculateMaxSpeed");
			MOD_ADD_DETOUR_MEMBER(CBaseEntity_TakeDamage ,"CBaseEntity::TakeDamage");
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_GetMaxBuffedHealth ,"CTFPlayerShared::GetMaxBuffedHealth");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_TakeHealth ,"CTFPlayer::TakeHealth");
			MOD_ADD_DETOUR_MEMBER(CObjectDispenser_DispenseMetal ,"CObjectDispenser::DispenseMetal");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_GiveAmmo ,"CTFPlayer::GiveAmmo");
			MOD_ADD_DETOUR_MEMBER(CTFGrenadePipebombProjectile_StickybombTouch ,"CTFGrenadePipebombProjectile::StickybombTouch");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_DropCurrencyPack ,"CTFPlayer::DropCurrencyPack");
			MOD_ADD_DETOUR_MEMBER(CTeamplayRoundBasedRules_GetMinTimeWhenPlayerMaySpawn ,"CTeamplayRoundBasedRules::GetMinTimeWhenPlayerMaySpawn");
			MOD_ADD_DETOUR_MEMBER(CTFGameRules_OnPlayerSpawned ,"CTFGameRules::OnPlayerSpawned");
			MOD_ADD_DETOUR_MEMBER(CTFMinigun_WindDown ,"CTFMinigun::WindDown");
			MOD_ADD_DETOUR_MEMBER(CTFMinigun_WindUp ,"CTFMinigun::WindUp");
			
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_DropAmmoPack, "CTFPlayer::DropAmmoPack");
			MOD_ADD_DETOUR_STATIC(CTFDroppedWeapon_Create, "CTFDroppedWeapon::Create");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_Regenerate ,"CTFPlayer::Regenerate");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_ItemsMatch ,"CTFPlayer::ItemsMatch");
			MOD_ADD_DETOUR_MEMBER(CTFMinigun_SetWeaponState ,"CTFMinigun::SetWeaponState");
			MOD_ADD_DETOUR_MEMBER(CTFFlameThrower_SetWeaponState ,"CTFFlameThrower::SetWeaponState");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Arrow_StrikeTarget ,"CTFProjectile_Arrow::StrikeTarget");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Arrow_FadeOut, "CTFProjectile_Arrow::FadeOut");
			MOD_ADD_DETOUR_MEMBER(CTFProjectile_Arrow_CheckSkyboxImpact, "CTFProjectile_Arrow::CheckSkyboxImpact");
			MOD_ADD_DETOUR_MEMBER(CTFPlayerShared_CalculateObjectCost, "CTFPlayerShared::CalculateObjectCost");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_GetPenetrateType, "CTFWeaponBase::GetPenetrateType");
			MOD_ADD_DETOUR_MEMBER(CBaseProjectile_GetCollideWithTeammatesDelay, "CBaseProjectile::GetCollideWithTeammatesDelay");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_TFPlayerThink,           "CTFPlayer::TFPlayerThink");
			MOD_ADD_DETOUR_MEMBER(CTFGameMovement_PlayerSolidMask, "CTFGameMovement::PlayerSolidMask");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_PlayerRunCommand,					 "CTFPlayer::PlayerRunCommand");
			MOD_ADD_DETOUR_MEMBER(CTFGameMovement_PreventBunnyJumping,			 "CTFGameMovement::PreventBunnyJumping");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_Reload,			 "CTFWeaponBase::Reload");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_Holster,			 "CTFWeaponBase::Holster");
			MOD_ADD_DETOUR_MEMBER(CBaseProjectile_D2, "CBaseProjectile::~CBaseProjectile [D2]");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBaseGun_PrimaryAttack, "CTFWeaponBaseGun::PrimaryAttack");
			MOD_ADD_DETOUR_STATIC(SV_ComputeClientPacks, "SV_ComputeClientPacks");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_Spawn, "CTFPlayer::Spawn");
			MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_GetSpreadAngles, "CTFWeaponBase::GetSpreadAngles");
			MOD_ADD_DETOUR_MEMBER(CAttributeList_SetRuntimeAttributeValue, "CAttributeList::SetRuntimeAttributeValue");
			MOD_ADD_DETOUR_MEMBER(CWeaponMedigun_RemoveHealingTarget, "CWeaponMedigun::RemoveHealingTarget");
			MOD_ADD_DETOUR_MEMBER(CWeaponMedigun_StartHealingTarget, "CWeaponMedigun::StartHealingTarget");
			MOD_ADD_DETOUR_STATIC(FX_FireBullets, "FX_FireBullets");
            MOD_ADD_DETOUR_MEMBER(CTFWeaponBase_Energy_Recharge, "CTFWeaponBase::Energy_Recharge");
            MOD_ADD_DETOUR_MEMBER(CWeaponMedigun_GetHealRate, "CWeaponMedigun::GetHealRate");
            MOD_ADD_DETOUR_MEMBER(CTFProjectile_EnergyBall_Explode, "CTFProjectile_EnergyBall::Explode");

		//  Implement disable alt fire
            MOD_ADD_DETOUR_MEMBER(CBasePlayer_ItemPostFrame, "CBasePlayer::ItemPostFrame");
			

		//	Remove knife armor penetration limit
			MOD_ADD_DETOUR_MEMBER(CTFKnife_GetMeleeDamage, "CTFKnife::GetMeleeDamage");
			
			//Inspect custom attributes
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_InspectButtonPressed ,"CTFPlayer::InspectButtonPressed");
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_UpdateOnRemove, "CTFPlayer::UpdateOnRemove");

			//MOD_ADD_DETOUR_MEMBER(CAttributeManager_OnAttributeValuesChanged, "CAttributeManager::OnAttributeValuesChanged");
			//MOD_ADD_DETOUR_MEMBER(CAttributeManager_OnAttributeValuesChanged, "CAttributeManager::OnAttributeValuesChanged");
			//MOD_ADD_DETOUR_MEMBER(CAttributeContainer_OnAttributeValuesChanged, "CAttributeContainer::OnAttributeValuesChanged");
			MOD_ADD_DETOUR_MEMBER(CAttributeContainerPlayer_OnAttributeValuesChanged, "CAttributeContainerPlayer::OnAttributeValuesChanged");
			//MOD_ADD_DETOUR_STATIC(CBaseEntity_EmitSound, "CBaseEntity::EmitSound [static: normal]");
			
		//	Allow explosive headshots on anything
			MOD_ADD_DETOUR_MEMBER(CTFPlayer_OnTakeDamage, "CTFPlayer::OnTakeDamage");

		//	Allow parsing attributes stored as integer
			MOD_ADD_DETOUR_MEMBER(CSchemaAttributeType_Default_ConvertStringToEconAttributeValue, "CSchemaAttributeType_Default::ConvertStringToEconAttributeValue");

		//	Allow set bonus attributes on items, with the help of custom attributes
			MOD_ADD_DETOUR_MEMBER(static_attrib_t_BInitFromKV_SingleLine, "static_attrib_t::BInitFromKV_SingleLine");
			
		//	Fix build small sentries attribute reaplly max health on redeploy bug
			MOD_ADD_DETOUR_MEMBER(CObjectSentrygun_MakeScaledBuilding, "CObjectSentrygun::MakeScaledBuilding");

		//  Fast attribute cache
			MOD_ADD_DETOUR_MEMBER(CAttributeManager_ClearCache,            "CAttributeManager::ClearCache");
			MOD_ADD_DETOUR_MEMBER_PRIORITY(CEconEntity_UpdateOnRemove,     "CEconEntity::UpdateOnRemove", LOWEST);
		}

		void LoadAttributes()
		{
			KeyValues *kv = new KeyValues("attributes");
			char path[PLATFORM_MAX_PATH];
			g_pSM->BuildPath(Path_SM,path,sizeof(path),"gamedata/sigsegv/custom_attributes.txt");
			if (kv->LoadFromFile(filesystem, path)) {
				DevMsg("Loaded attrs\n");
				CUtlVector<CUtlString> err;
				GetItemSchema()->BInitAttributes(kv, &err);
			}
		}

		virtual bool OnLoad() override
		{
			if (GetItemSchema() != nullptr)
				LoadAttributes();
			return true;
		}

		virtual void LevelInitPostEntity() override
		{
			precached.clear();
			entity_penetration_counter.clear();
			player_touch_times.clear();
		}

		virtual void LevelInitPreEntity() override
		{
			precached.clear();
			entity_penetration_counter.clear();
			LoadAttributes();
		}
		virtual bool ShouldReceiveCallbacks() const override { return true; }

		virtual void OnUnload() override
		{
			precached.clear();
			entity_penetration_counter.clear();
		}
		
		virtual void OnDisable() override
		{
			precached.clear();
			entity_penetration_counter.clear();
			player_touch_times.clear();
		}

		virtual void OnEnable() override
		{
			LoadItemNames();
		}

		virtual void FrameUpdatePostEntityThink() override
		{
			if (gpGlobals->tickcount % 16 == 0) { 
				for (auto it = entity_penetration_counter.begin(); it != entity_penetration_counter.end(); ) {
					if (it->first == nullptr || it->first->IsMarkedForDeletion()) {
						it = entity_penetration_counter.erase(it);
					}
					else {
						it++;
					}
				}
				ForEachTFPlayer([&](CTFPlayer *player){
					static bool in_upgrade_zone[33];

					if (player->IsBot()) return;

					int index = ENTINDEX(player) - 1;
					if (player->m_Shared->m_bInUpgradeZone) {
						in_upgrade_zone[index] = true;
						if (attribute_info_strings[index].empty()) {
							InspectAttributes(player, player, false);
						} 
					}
					else if (in_upgrade_zone[index]) {
						in_upgrade_zone[index] = false;
						attribute_info_strings[index].clear();
						ClearAttributeDisplay(player);
					}
				});
			}

			for (size_t i = 0; i < model_entries.size(); ) {
				auto &entry = model_entries[i];
				if (entry.weapon == nullptr || entry.weapon->IsMarkedForDeletion()) {
					if (entry.wearable != nullptr)
						entry.wearable->Remove();
					if (entry.wearable_vm != nullptr)
						entry.wearable_vm->Remove();
					
					model_entries.erase(model_entries.begin() + i);
					continue;
				}
				
				if (!entry.weapon->m_bBeingRepurposedForTaunt)
					entry.weapon->m_bBeingRepurposedForTaunt = true;

				if (entry.wearable_vm == nullptr || entry.wearable == nullptr) {
					if (entry.wearable != nullptr)
						entry.wearable->Remove();
					if (entry.wearable_vm != nullptr)
						entry.wearable_vm->Remove();

					CreateWeaponWearables(entry);
				}
				if (entry.weapon->IsEffectActive(EF_NODRAW)) {
					/*if (entry.wearable != nullptr)
						entry.wearable->Remove();
					if (entry.wearable_vm != nullptr)
						entry.wearable_vm->Remove();*/
					if (entry.wearable != nullptr && !entry.wearable->IsEffectActive(EF_NODRAW))
						entry.wearable->SetEffects(entry.wearable->GetEffects() | EF_NODRAW);
					if (entry.wearable_vm != nullptr && !entry.wearable_vm->IsEffectActive(EF_NODRAW))
						entry.wearable_vm->SetEffects(entry.wearable_vm->GetEffects() | EF_NODRAW);
				}
				else {
					/*if (entry.wearable == nullptr) {
						CreateWeaponWearables(entry);
					}*/
					if (entry.wearable != nullptr && entry.wearable->IsEffectActive(EF_NODRAW))
						entry.wearable->SetEffects(entry.wearable->GetEffects() & ~(EF_NODRAW));
					if (entry.wearable_vm != nullptr && entry.wearable_vm->IsEffectActive(EF_NODRAW))
						entry.wearable_vm->SetEffects(entry.wearable_vm->GetEffects() & ~(EF_NODRAW));
				}
				i++;
			}
			for (size_t i = 0; i < stick_info.size(); ) {
				auto &entry = stick_info[i];
				if (entry.sticky == nullptr) {
					stick_info.erase(stick_info.begin() + i);
					continue;
				}
				auto phys = entry.sticky->VPhysicsGetObject();
				if (entry.sticked == nullptr || !entry.sticked->IsAlive()) {
					if (phys != nullptr) {
						phys->EnableMotion(true);
					}
					stick_info.erase(stick_info.begin() + i);
					continue;
				}
				if (phys != nullptr && phys->IsMotionEnabled()) {
					stick_info.erase(stick_info.begin() + i);
					continue;
				}
				entry.sticky->SetAbsOrigin(entry.sticked->GetAbsOrigin() + entry.offset);
				i++;
			}
			// The function does not make use of this pointer, so its safe to convert to CAttributeManager
			static int last_cache_version = 0;
            int cache_version = reinterpret_cast<CAttributeManager *>(this)->GetGlobalCacheVersion();

            if(last_cache_version != cache_version) {
                for (int i = 0; i < 2048; i++) {
					auto cache = fast_attribute_cache[i];
					if (cache != nullptr) {
						int count = i <= gpGlobals->maxClients ? ATTRIB_COUNT_PLAYER : ATTRIB_COUNT_ITEM;
						for(int i = 0; i < count; i++) {
							cache[i] = FLT_MIN;
						}
					}
                }
                last_cache_version = cache_version;
            }
		}
	};
	CMod s_Mod;
	
	
	ConVar cvar_enable("sig_attr_custom", "0", FCVAR_NOTIFY,
		"Mod: enable custom attributes",
		[](IConVar *pConVar, const char *pOldValue, float flOldValue){
			s_Mod.Toggle(static_cast<ConVar *>(pConVar)->GetBool());
		});

}
