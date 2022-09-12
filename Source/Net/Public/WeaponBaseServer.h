// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "WeaponBaseClient.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "WeaponBaseServer.generated.h"

//创建枚举类型
UENUM()
enum class EWeaponType : uint8
{
	Ak47 UMETA(Displayname = "Ak47"),
	DesertEagle UMETA(Displayname = "DesertEagle")
};


UCLASS()
class NET_API AWeaponBaseServer : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWeaponBaseServer();

	UPROPERTY(EditAnywhere)
	EWeaponType KindofWeapon;

	UPROPERTY(EditAnywhere)
	USkeletalMeshComponent* WeaponMesh;

	UPROPERTY(EditAnywhere)
	USphereComponent* SphereCollision;

	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TSubclassOf<AWeaponBaseClient> ClientWeaponBaseBpClass;


	UPROPERTY(EditAnywhere)
	USoundBase* FireSound;
	
	UPROPERTY(EditAnywhere)
	UParticleSystem* MuzzleFlash;

	UPROPERTY(EditAnywhere)
	UAnimMontage* ServerTPBodysShootAnimMontage;

	UPROPERTY(EditAnywhere)
	UAnimMontage* ServerTPBodysReloadAnimMontage;

	UPROPERTY(EditAnywhere,Replicated)//Replicated 指服务器改变客户端也会改变
	int32 GunCurrentAmmo;//枪体的子弹
	
	UPROPERTY(EditAnywhere,Replicated)
	int32 ClipCurrentAmmo;//弹夹的子弹

	UPROPERTY(EditAnywhere,Replicated)
	int32 MAXClipAmmo;//弹夹容量

	UPROPERTY(EditAnywhere)
	float BulluetDistance;

	UPROPERTY(EditAnywhere)
	float BaseDmage;
	
	UFUNCTION()
	void OnOtherBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult);

	UFUNCTION()
	void EquipWeapon();

	UFUNCTION(NetMulticast,Reliable,WithValidation)
	void MultiShootingEffect();//调用的名字
	void MultiShootingEffect_Implementation(); //方法体实现
	bool MultiShootingEffect_Validate();

	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
