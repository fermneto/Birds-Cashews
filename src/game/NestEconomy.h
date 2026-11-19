#pragma once
#include "save/Profile.h"

// Preco para subir do nivel informado ao seguinte. Retorna 0 se o upgrade ja estiver no maximo.
int upgradePrice(BirdUpgrade upgrade, int level);
int upgradePrice(NestUpgrade upgrade, int level);

const char* upgradeName(BirdUpgrade upgrade);
const char* upgradeName(NestUpgrade upgrade);

// Codigo do upgrade no campo value de GameEvent: passaro de 0 a 3, ninho de 4 a 7.
int upgradeCode(BirdUpgrade upgrade);
int upgradeCode(NestUpgrade upgrade);

// Efeito de cada nivel nos valores reais do jogo.
float speedMultiplier(int level);
float agilityMultiplier(int level);
int carryCapacity(int level);
float collectRadius(int level);
float nestScale(int sizeLevel);

// Moedas extras de uma entrega. Nao contam na pontuacao nem no record.
int sizeCoinBonus(int delivered, int sizeLevel);
