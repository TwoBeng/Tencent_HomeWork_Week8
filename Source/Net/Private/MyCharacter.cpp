// Fill out your copyright notice in the Description page of Project Settings.


#include "MyCharacter.h"

//#include "AudioMixerDevice.h"
//#include "EditorTutorial.h"
#include "ParticleHelper.h"
//#include "Evaluation/IMovieSceneEvaluationHook.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

// Sets default values
AMyCharacter::AMyCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

 #pragma region Component
	//构造函数里面初始化.h文件的声明的变量，如 相机组件和第一人称手臂组件
	PlayerCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PlayerCamera"));
	if(PlayerCamera)//判断指针是否有效
	{
		PlayerCamera->SetupAttachment(RootComponent);//将摄像机组件添加到根组件
		PlayerCamera->bUsePawnControlRotation = true;//摄像机的旋转（绕pitch）
	}
	FPArmMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FPArmMesh"));
	if(FPArmMesh)
	{
		FPArmMesh->SetupAttachment(PlayerCamera);
		FPArmMesh->SetOnlyOwnerSee(true);//设置第一人称的手臂模型只能自己能看到
	}
	Mesh->SetOwnerNoSee(true);//这个是继承Character的Mesh，相当于第三人称的Mesh，设置它不能被自己看见

	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);//设置碰撞，外部的碰撞由胶囊体提供，所以设置QueryOnly
	Mesh->SetCollisionObjectType(ECollisionChannel::ECC_Pawn);
#pragma endregion 
}

void AMyCharacter::DelayPlayControlCallBack()
{
	FPSPlayerController = Cast<AMyPlayerController>(GetController());
	if(FPSPlayerController)
	{
		FPSPlayerController->CreatePlayerUI();
	}
	else
	{
		FLatentActionInfo ActionInfo;
		ActionInfo.CallbackTarget = this;
		ActionInfo.ExecutionFunction = TEXT("DelayPlayControlCallBack");
		ActionInfo.UUID = FMath::Rand();
		ActionInfo.Linkage = 0;
		UKismetSystemLibrary::Delay(this,0.5,ActionInfo);
		
	}
}


// Called when the game starts or when spawned
void AMyCharacter:: BeginPlay()
{
	Super::BeginPlay();
	Health = 100;
	IsFireing = false;
	IsReloading = false;
	OnTakePointDamage.AddDynamic(this,&AMyCharacter::OnHit);//事件发生就会调用OnHit事件，回调函数  类似于球形碰撞体，当碰撞时就回调 详见WeaponServer

	//播放动画赋予动画蓝图初始值
	ClientArmsAnimBP = FPArmMesh->GetAnimInstance();
	ServerBodyAnimBP = Mesh->GetAnimInstance();
	
	FPSPlayerController = Cast<AMyPlayerController>(GetController());
	if(FPSPlayerController)
	{
		FPSPlayerController->CreatePlayerUI();
	}
	else
	{
		FLatentActionInfo ActionInfo;
		ActionInfo.CallbackTarget = this;
		ActionInfo.ExecutionFunction = TEXT("DelayPlayControlCallBack");
		ActionInfo.UUID = FMath::Rand();
		ActionInfo.Linkage = 0;
		UKismetSystemLibrary::Delay(this,0.5,ActionInfo);
		
	}
	StartWithKindofWeapon();
}



void AMyCharacter:: GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const//重载父类函数  让子弹数也能replicate到客户端
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);//调用父类的方法，避免崩溃  有的会用到
	DOREPLIFETIME_CONDITION(AMyCharacter,IsFireing,COND_None);
	DOREPLIFETIME_CONDITION(AMyCharacter,IsReloading,COND_None);
}

// Called every frame
void AMyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	InputComponent->BindAction(TEXT("LowSpeedWalk"),IE_Pressed,this,&AMyCharacter::LowSpeedWalkAction);
	InputComponent->BindAction(TEXT("LowSpeedWalk"),IE_Released,this,&AMyCharacter::NormalSpeedWalkAction);

	InputComponent->BindAction(TEXT("Jump"),IE_Pressed,this,&AMyCharacter::JumpAction);
	InputComponent->BindAction(TEXT("Jump"),IE_Released,this,&AMyCharacter::StopJumpAction);

	InputComponent->BindAction(TEXT("Fire"),IE_Pressed,this,&AMyCharacter::FireAction);
	InputComponent->BindAction(TEXT("Fire"),IE_Released,this,&AMyCharacter::StopFireAction);

	InputComponent->BindAction(TEXT("Reload"),IE_Pressed,this,&AMyCharacter::ReloadAction);
	
	InputComponent->BindAxis(TEXT("MoveForward"),this,&AMyCharacter::MoveForward);
	InputComponent->BindAxis(TEXT("MoveRight"),this,&AMyCharacter::MoveRight);

	InputComponent->BindAxis(TEXT("Turn"),this,&AMyCharacter::AddControllerYawInput);
	InputComponent->BindAxis(TEXT("LookUp"),this,&AMyCharacter::AddControllerPitchInput);
	
}


#pragma  region NetWorking
void AMyCharacter::ServerLowSpeedWalkAction_Implementation()
{
	CharacterMovement->MaxWalkSpeed = 300;
}

bool AMyCharacter::ServerLowSpeedWalkAction_Validate()
{
	return true;
}

void AMyCharacter::ServerNormalSpeedWalkAction_Implementation()
{
	CharacterMovement->MaxWalkSpeed = 600;
}

bool AMyCharacter::ServerNormalSpeedWalkAction_Validate()
{
	return true;
}

void AMyCharacter::ServerFireRifleAction_Implementation(FVector CameraLocation, FRotator CameraRotation, bool IsMoving)
{
	if(ServerPrimaryWeapon)
	{
		//多播（只在服务器调用，谁调谁多播）
		ServerPrimaryWeapon->MultiShootingEffect();//枪口闪光以及声音

		ServerPrimaryWeapon->ClipCurrentAmmo -= 1;
		ClientUpdateAmmoUI(ServerPrimaryWeapon->ClipCurrentAmmo,ServerPrimaryWeapon->GunCurrentAmmo);//RPC在客户端执行，更新弹药UI

		//多播身体射击蒙太奇（让其他人看到你在射击）
		MultiShooting();
		IsFireing = true;
		
		RifeLineTrace(CameraLocation, CameraRotation, IsMoving);
	}
	

	
}

bool AMyCharacter::ServerFireRifleAction_Validate(FVector CameraLocation, FRotator CameraRotation, bool IsMoving)
{
	return true;
}

void AMyCharacter::ServerReloadPrimary_Implementation()
{
	//客户端播放手臂动画  服务端身体多播动画  数据更新  UI
	if(ServerPrimaryWeapon)
	{
		if(ServerPrimaryWeapon->GunCurrentAmmo > 0 && ServerPrimaryWeapon->ClipCurrentAmmo < ServerPrimaryWeapon->MAXClipAmmo)
		{
			ClientReload();
			MultiReloadAction();
			IsReloading = true;
			if(ClientPrimaryWeapon)
			{
				FLatentActionInfo ActionInfo;
				ActionInfo.CallbackTarget = this;
				ActionInfo.ExecutionFunction = TEXT("DelayPlayArmReloadCallBack");
				ActionInfo.UUID = FMath::Rand();
				ActionInfo.Linkage = 0;
				UKismetSystemLibrary::Delay(this,ClientPrimaryWeapon->ClientArmsReloadAnimMontage->GetPlayLength(),ActionInfo);
			}
		}
	}
	
	// UKismetSystemLibrary::PrintString(GetWorld(),FString::Printf(TEXT("Reload")));
	
}

bool AMyCharacter::ServerReloadPrimary_Validate()
{
	return true;
}

void AMyCharacter::ServerStopFiring_Implementation()
{
	IsFireing = false;
}

bool AMyCharacter::ServerStopFiring_Validate()
{
	return true;
}

void AMyCharacter::ClientUpdateAmmoUI_Implementation(int32 ClipCurrentAmmo, int32 GunCurrentAmmo)
{
	if(FPSPlayerController)
	{
		FPSPlayerController->UpdateAmmoUI(ClipCurrentAmmo,GunCurrentAmmo);
	}
}

void AMyCharacter::ClientFire_Implementation()
{
	//枪体动画
	AWeaponBaseClient* CurrentClientWeapon = GetCurrentClientFPArmsWeaponActor();
	if(CurrentClientWeapon)
	{
		CurrentClientWeapon->PlayShootAnimation();
		//手臂动画
		UAnimMontage* ClientArmFireMontage = CurrentClientWeapon->ClientArmsFireAnimMontage;
		ClientArmsAnimBP->Montage_SetPlayRate(ClientArmFireMontage,1);
		ClientArmsAnimBP->Montage_Play(ClientArmFireMontage);

		//播放声音
		CurrentClientWeapon->DisaplayWeaponEffect();

		//镜头抖动
		FPSPlayerController->PlayerCameraShake(CurrentClientWeapon->CameraShakeClass);

		
	}

	
}

void AMyCharacter::ClientEquipFPArmsPrimary_Implementation()
{
	if(ServerPrimaryWeapon)
	{
		if(ClientPrimaryWeapon)
		{
			
		}
		else
		{
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.Owner = this;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ClientPrimaryWeapon = GetWorld()->SpawnActor<AWeaponBaseClient>(ServerPrimaryWeapon->ClientWeaponBaseBpClass,
				GetActorTransform(),
				SpawnInfo);
			ClientPrimaryWeapon->K2_AttachToComponent(FPArmMesh,TEXT("WeaponSocket"),EAttachmentRule::SnapToTarget,
				EAttachmentRule::SnapToTarget,
				EAttachmentRule::SnapToTarget,
				true);
			ClientUpdateAmmoUI(ServerPrimaryWeapon->ClipCurrentAmmo,ServerPrimaryWeapon->GunCurrentAmmo);
			//改变手臂动画
		}
	}
}


void AMyCharacter::MultiShooting_Implementation()
{
	if(ServerBodyAnimBP)
	{
		if(ServerPrimaryWeapon)
		{
			ServerBodyAnimBP->Montage_Play(ServerPrimaryWeapon->ServerTPBodysShootAnimMontage);
		}
	}
}

bool AMyCharacter::MultiShooting_Validate()
{
	return true;
}

void AMyCharacter::MultiReloadAction_Implementation()
{

	AWeaponBaseServer* ServerCurrentWeapon = GetCurrentServerTPWeaponActor();
	if(ServerBodyAnimBP)
	{
		if(ServerCurrentWeapon)
		{
			UAnimMontage* ServerAimMontage = ServerCurrentWeapon->ServerTPBodysReloadAnimMontage;
			ServerBodyAnimBP->Montage_Play(ServerAimMontage);
			
		}
	}
}

bool AMyCharacter::MultiReloadAction_Validate()
{
	return true;
}

void AMyCharacter::MultiDeathAmim_Implementation()
{
	if(ServerBodyAnimBP)
	{
		
		ServerBodyAnimBP->Montage_Play(ServerTpBodysDeathAnimMontage);
			
		
	}
}

bool AMyCharacter::MultiDeathAmim_Validate()
{
	return true;
}

void AMyCharacter::ClientDeathMatchDeath_Implementation()
{
	AWeaponBaseClient* ClientCurrentWeapon = GetCurrentClientFPArmsWeaponActor();
	
	if(ClientCurrentWeapon)
	{
		ClientCurrentWeapon->Destroy();
	}

	if(FPArmMesh)
	{
		FPArmMesh->DestroyComponent();
	}
}

void AMyCharacter::ClientReload_Implementation()
{
	AWeaponBaseClient* ClientCurrentWeapon = GetCurrentClientFPArmsWeaponActor();
	if(ClientCurrentWeapon)
	{
		UAnimMontage* ClientAimMontage = ClientCurrentWeapon->ClientArmsReloadAnimMontage;
		ClientArmsAnimBP->Montage_Play(ClientAimMontage);
		ClientCurrentWeapon->PlayReloadAnimation();
	}
	
}

void AMyCharacter::ClientUpdateHealthUi_Implementation(float NewHealth)
{
	if(FPSPlayerController)
	{
		FPSPlayerController->UpdateHealthUI(NewHealth);
	}
	
}

#pragma endregion

#pragma region InputEvent
void AMyCharacter::MoveRight(float AxisValue)
{
	AddMovementInput(GetActorRightVector(),AxisValue,false);
}

void AMyCharacter::MoveForward(float AxisValue)
{
	AddMovementInput(GetActorForwardVector(),AxisValue,false);
}

void AMyCharacter::JumpAction()
{
	 Jump();
}

void AMyCharacter::StopJumpAction()
{
	StopJumping();
}

void AMyCharacter::LowSpeedWalkAction()
{
	CharacterMovement->MaxWalkSpeed = 300;
	ServerLowSpeedWalkAction();
}

void AMyCharacter::NormalSpeedWalkAction()
{
	CharacterMovement->MaxWalkSpeed = 600;
	ServerNormalSpeedWalkAction();
}

void AMyCharacter::FireAction()
{
	switch (ActiveWeapon)
	{
	case EWeaponType::Ak47:
		{
			FireWeaponPrimary();
		}
		break;
	}
}

void AMyCharacter::StopFireAction()
{
	switch (ActiveWeapon)
	{
	case EWeaponType::Ak47:
		{
			StopFireWeaponPrimary();
		}
		break;
	}
}
//换弹
void AMyCharacter::ReloadAction()
{
	if(!IsReloading)//如果正在播放动画 就不播放了  避免鬼畜
	{
		if(!IsFireing)
		{
			switch (ActiveWeapon)
			{
			case EWeaponType::Ak47:
				{
					ServerReloadPrimary();
				}
			}
		}
	}
}


#pragma endregion  


#pragma region Weapon

void AMyCharacter::DelayPlayArmReloadCallBack()
{
	int32 GunCurrentAmmo = ServerPrimaryWeapon->GunCurrentAmmo;
	int32 ClipCurrentAmmo = ServerPrimaryWeapon->ClipCurrentAmmo;
	int32 const MAXClipAmmo = ServerPrimaryWeapon->MAXClipAmmo;
	if(MAXClipAmmo - ClipCurrentAmmo >= GunCurrentAmmo)
	{
		ClipCurrentAmmo += GunCurrentAmmo;
		GunCurrentAmmo = 0;
		
	}
	else
	{
		GunCurrentAmmo -= MAXClipAmmo - ClipCurrentAmmo;
		ClipCurrentAmmo = MAXClipAmmo;
		
	}
	IsReloading = false;
	ServerPrimaryWeapon->GunCurrentAmmo = GunCurrentAmmo;
	ServerPrimaryWeapon->ClipCurrentAmmo = ClipCurrentAmmo;
	ClientUpdateAmmoUI(ServerPrimaryWeapon->ClipCurrentAmmo,ServerPrimaryWeapon->GunCurrentAmmo);

	// UKismetSystemLibrary::PrintString(GetWorld(),FString::Printf(TEXT("Delay")));
}

void AMyCharacter::EquipPrimary(AWeaponBaseServer* WeaponBaseServer)
{
	 if(ServerPrimaryWeapon)
	 {
		 
	 }
	 else
	 {
	 	ServerPrimaryWeapon = WeaponBaseServer;
	 	ServerPrimaryWeapon->SetOwner(this);
	 	ServerPrimaryWeapon->K2_AttachToComponent(Mesh,TEXT("hand_rSocket"),EAttachmentRule::SnapToTarget,
	 		EAttachmentRule::SnapToTarget,
	 		EAttachmentRule::SnapToTarget,
	 		true );
	 	ActiveWeapon = ServerPrimaryWeapon->KindofWeapon;
	 	ClientEquipFPArmsPrimary();
	 }
}


void AMyCharacter::StartWithKindofWeapon()
{
	if(HasAuthority())
	{
		PurchaseWeapon(EWeaponType::Ak47);
	}
}

void AMyCharacter::PurchaseWeapon(EWeaponType WeaponType)
{
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Owner = this;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	switch (WeaponType)
	{
	case EWeaponType::Ak47:
		{
			AWeaponBaseServer* ServerWeapon;
			// if(!ServerPrimaryWeapon)
			
				UClass* BluePrintVar = StaticLoadClass(AWeaponBaseServer::StaticClass(),nullptr,TEXT("Blueprint'/Game/BluePrint/Weapon/Ak47/Server_Ak47_BP.Server_Ak47_BP_C'"));
				ServerWeapon = GetWorld()->SpawnActor<AWeaponBaseServer>(BluePrintVar,
					GetActorTransform(),
					SpawnInfo);
				IsHaveServerWeaon = true;
			
			// else
			// {
			// 	ServerWeapon = ServerPrimaryWeapon;
			// }
				ServerWeapon->EquipWeapon();//服务器调用装备武器这个函数，动态生成第三人称武器（baseonserver）,没碰撞，调用EquipPrimary装备武器，因为你第三人称武器是replicate，客户端也会复制一份，有碰撞，就走碰撞装备武器
				EquipPrimary(ServerWeapon);
			
		}
		break;
		default:
			{
				 
			}
	}
}

AWeaponBaseClient* AMyCharacter::GetCurrentClientFPArmsWeaponActor()
{
	switch (ActiveWeapon)
	{
	case EWeaponType::Ak47:
		{
			return ClientPrimaryWeapon;
		}
	}
	return nullptr;
}

AWeaponBaseServer* AMyCharacter::GetCurrentServerTPWeaponActor()
{
	switch (ActiveWeapon)
	{
	case EWeaponType::Ak47:
		{
			return ServerPrimaryWeapon;
		}
	}
	return nullptr;
}
#pragma endregion 

#pragma region Fire
void AMyCharacter::FireWeaponPrimary()
{
	//服务端（减少弹药，射线检测（三种），伤害应用，单孔生成）

	
	//客户端：（枪体动画播放，手臂动画，播放声音，应用屏幕抖动，后坐力，枪口的闪光效果）
	//联机系统开发
	if(ServerPrimaryWeapon->ClipCurrentAmmo > 0 && !IsReloading)
	{
		ServerFireRifleAction(PlayerCamera->GetComponentLocation(),PlayerCamera->GetComponentRotation(),false);
		ClientFire();

		
	}
	

}

void AMyCharacter::StopFireWeaponPrimary()
{
	//IsFireing = false;需要在服务器上实现
	ServerStopFiring();
}



void AMyCharacter::RifeLineTrace(FVector CameraLocation, FRotator CameraRotation, bool IsMoving)
{
	FVector EndLocation;
	FVector CameraForwardVector = UKismetMathLibrary::GetForwardVector(CameraRotation);
	TArray<AActor*> IgnoreArray;
	IgnoreArray.Add(this);

	FHitResult HitResult;//传入传出参数，告诉碰撞的结果
	
	if(ServerPrimaryWeapon)
	{
		if(IsMoving)
		{
		
		}
		else
		{
			EndLocation = CameraLocation + CameraForwardVector * ServerPrimaryWeapon->BulluetDistance;
		}
		
	}
	bool HitSuccess =  UKismetSystemLibrary::LineTraceSingle(GetWorld(),CameraLocation,EndLocation,ETraceTypeQuery::TraceTypeQuery1,false,IgnoreArray,
		EDrawDebugTrace::None,HitResult,true,FLinearColor::Red,FLinearColor::Green,3.f
		);
	if(HitSuccess)
	{
		AMyCharacter* HitCharactor = Cast<AMyCharacter>(HitResult.Actor);
		if(HitCharactor)
		{
			// UKismetSystemLibrary::PrintString(GetWorld(),FString::Printf(TEXT("Hit Name : %s"), *HitResult.Actor->GetName()));
			DamegePlayer(HitResult.PhysMaterial.Get(),HitResult.Actor.Get(),CameraLocation,HitResult);
		}
		
		//打到玩家应用伤害
		//打到其他生成弹孔
	}
}

void AMyCharacter::DamegePlayer(UPhysicalMaterial* PhysicalMaterial,AActor* DamegeActor,FVector& HitDirection,FHitResult& HitInfo)
{
	float Damege = 0;
	if(ServerPrimaryWeapon)
	{
		switch (PhysicalMaterial->SurfaceType)
		{
		case EPhysicalSurface::SurfaceType1:
			{
				//头
				Damege = ServerPrimaryWeapon->BaseDmage * 4;
			}
			break;
		case EPhysicalSurface::SurfaceType2:
			{
				//Body
				Damege = ServerPrimaryWeapon->BaseDmage * 1;
			}
			break;
		case EPhysicalSurface::SurfaceType3:
			{
				//Arm
				Damege = ServerPrimaryWeapon->BaseDmage * 0.8;
			}
			break;
		case EPhysicalSurface::SurfaceType4:
			{
				//Leg
				Damege = ServerPrimaryWeapon->BaseDmage * 0.5;
			}
			break;
		}
		//五个位置不同伤害
	
		UGameplayStatics::ApplyPointDamage(DamegeActor,Damege,HitDirection,HitInfo,
			GetController(),this,UDamageType::StaticClass());//打了别人调这个发通知
		OnTakePointDamage;//给接受伤害的添加回调，自己被打的时候，就会被调用
	}
	}
	

void AMyCharacter::OnHit(AActor* DamagedActor, float Damage, AController* InstigatedBy, FVector HitLocation,
	UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const UDamageType* DamageType,
	AActor* DamageCauser)
{
	Health -= Damage;
	ClientUpdateHealthUi(Health);
	//更新生命UI 步骤：1、客户端RPC（服务器调用在客户端执行的RPC） 2、1的函数实现 ：调用玩家控制器的更新UI函数 3 2的函数在蓝图实现
	if(Health <= 0)
	{
		//死亡
		DeathMatchDeath(DamageCauser);
		MultiDeathAmim();
	}
	// UKismetSystemLibrary::PrintString(GetWorld(),FString::Printf(TEXT("Health : %f"), Health));
}

void AMyCharacter::DeathMatchDeath(AActor* Damager)
{
	AWeaponBaseServer* CurrentServerWeapon = GetCurrentServerTPWeaponActor();
	AWeaponBaseClient* CurrentClientWeapon = GetCurrentClientFPArmsWeaponActor();
	if(CurrentServerWeapon)
	{
		CurrentServerWeapon->Destroy();
	}
	if(CurrentClientWeapon)
	{
		CurrentClientWeapon->Destroy();
	}
	ClientDeathMatchDeath();
	
	AMyPlayerController* MultiPlaterController = Cast<AMyPlayerController>(GetController());
	if(MultiPlaterController)
	{
		MultiPlaterController->DeathMatchDeath(Damager);
	}
}

#pragma endregion


