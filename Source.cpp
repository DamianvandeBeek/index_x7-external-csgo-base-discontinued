#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include "offsets.h"

using namespace std;

bool triggerbot_triggered = false;
bool toggle_triggerbot = true;
bool toggle_wallhack = true;
bool toggle_aimbot = true;
bool toggle_bunnyhop = true;


const int SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN); const int xhairx = SCREEN_WIDTH / 2;
const int SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN); const int xhairy = SCREEN_HEIGHT / 2;

uintptr_t moduleBase;
DWORD procId;
HWND hwnd;
HANDLE hProcess;
DWORD client;
HDC hdc;
int closest;



uintptr_t GetModuleBaseAddress(const char* modName) {
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
	if (hSnap != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 modEntry;
		modEntry.dwSize = sizeof(modEntry);
		if (Module32First(hSnap, &modEntry)) {
			do {
				if (!strcmp(modEntry.szModule, modName)) {
					CloseHandle(hSnap);
					return (uintptr_t)modEntry.modBaseAddr;
				}
			} while (Module32Next(hSnap, &modEntry));
		}
	}
}

template<typename T> T RPM(SIZE_T address) {
	T buffer;
	ReadProcessMemory(hProcess, (LPCVOID)address, &buffer, sizeof(T), NULL);
	return buffer;
}

template<typename T> void WPM(SIZE_T address, T buffer) {
	WriteProcessMemory(hProcess, (LPVOID)address, &buffer, sizeof(buffer), NULL);
}

struct glowStructEnemy {
	float red = 0.85f;
	float green = 0.f;
	float blue = 0.85f;
	float alpha = 0.9f;
	uint8_t padding[8];
	float unknown = 1.f;
	uint8_t padding2[4];
	BYTE renderOccluded = true;
	BYTE renderUnoccluded = false;
	BYTE fullBloom = false;
}glowEnm;

struct glowStructLocal {
	float red = 0.f;
	float green = 0.25f;
	float blue = 0.65f;
	float alpha = 0.9f;
	uint8_t padding[8];
	float unknown = 1.f;
	uint8_t padding2[4];
	BYTE renderOccluded = true;
	BYTE renderUnoccluded = false;
	BYTE fullBloom = false;
}glowLocal;



class Vector3 {
public:
	float x, y, z;
	Vector3() : x(0.f), y(0.f), z(0.f) {}
	Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};


int getTeam(uintptr_t player) {
	return RPM<int>(player + m_iTeamNum);
}

int getCrosshairID(uintptr_t player) {
	return RPM<int>(player + m_iCrosshairId);
}

uintptr_t GetLocalPlayer() {
	return RPM< uintptr_t>(moduleBase + dwLocalPlayer);
}

uintptr_t GetPlayer(int index) {  //Each player has an index. 1-64
	return RPM< uintptr_t>(moduleBase + dwEntityList + index * 0x10); //We multiply the index by 0x10 to select the player we want in the entity list.
}

int GetPlayerHealth(uintptr_t player) {
	return RPM<int>(player + m_iHealth);
}

Vector3 PlayerLocation(uintptr_t player) { //Stores XYZ coordinates in a Vector3.
	return RPM<Vector3>(player + m_vecOrigin);
}

bool DormantCheck(uintptr_t player) {
	return RPM<int>(player + m_bDormant);
}

Vector3 get_head(uintptr_t player) {
	struct boneMatrix_t {
		byte pad3[12];
		float x;
		byte pad1[12];
		float y;
		byte pad2[12];
		float z;
	};
	uintptr_t boneBase = RPM<uintptr_t>(player + m_dwBoneMatrix);
	boneMatrix_t boneMatrix = RPM<boneMatrix_t>(boneBase + (sizeof(boneMatrix) * 8 /*8 is the boneid for head*/));
	return Vector3(boneMatrix.x, boneMatrix.y, boneMatrix.z);
}

struct view_matrix_t {
	float matrix[16];
} vm;

struct Vector3 WorldToScreen(const struct Vector3 pos, struct view_matrix_t matrix) { //This turns 3D coordinates (ex: XYZ) int 2D coordinates (ex: XY).
	struct Vector3 out;
	float _x = matrix.matrix[0] * pos.x + matrix.matrix[1] * pos.y + matrix.matrix[2] * pos.z + matrix.matrix[3];
	float _y = matrix.matrix[4] * pos.x + matrix.matrix[5] * pos.y + matrix.matrix[6] * pos.z + matrix.matrix[7];
	out.z = matrix.matrix[12] * pos.x + matrix.matrix[13] * pos.y + matrix.matrix[14] * pos.z + matrix.matrix[15];

	_x *= 1.f / out.z;
	_y *= 1.f / out.z;

	out.x = SCREEN_WIDTH * .5f;
	out.y = SCREEN_HEIGHT * .5f;

	out.x += 0.5f * _x * SCREEN_WIDTH + 0.5f;
	out.y -= 0.5f * _y * SCREEN_HEIGHT + 0.5f;

	return out;
}

float pythag(int x1, int y1, int x2, int y2) {
	return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}

int FindClosestEnemy() {
	float Finish;
	int ClosestEntity = 1;
	Vector3 Calc = { 0, 0, 0 };
	float Closest = FLT_MAX;
	int localTeam = getTeam(GetLocalPlayer());
	for (int i = 1; i < 64; i++) { //Loops through all the entitys in the index 1-64.
		DWORD Entity = GetPlayer(i);
		int EnmTeam = getTeam(Entity); if (EnmTeam == localTeam) continue;
		int EnmHealth = GetPlayerHealth(Entity); if (EnmHealth < 1 || EnmHealth > 100) continue;
		int Dormant = DormantCheck(Entity); if (Dormant) continue;
		Vector3 headBone = WorldToScreen(get_head(Entity), vm);
		Finish = pythag(headBone.x, headBone.y, xhairx, xhairy);
		if (Finish < Closest) {
			Closest = Finish;
			ClosestEntity = i;
		}
	}

	return ClosestEntity;
}

void DrawLine(float StartX, float StartY, float EndX, float EndY) { //This function is optional for debugging.
	int a, b = 0;
	HPEN hOPen;
	HPEN hNPen = CreatePen(PS_SOLID, 2, 0x0000FF /*red*/);
	hOPen = (HPEN)SelectObject(hdc, hNPen);
	MoveToEx(hdc, StartX, StartY, NULL); //start of line
	a = LineTo(hdc, EndX, EndY); //end of line
	DeleteObject(SelectObject(hdc, hOPen));
}

void FindClosestEnemyThread() {
	while (1) {
		closest = FindClosestEnemy();
	}
}


int main() {

	cout << "Index_x7 Cheat loaded...\n\n";
	cout << "Toggle Wallhack with F1\n";
	cout << "Toggle Triggerbot with F2\n";
	cout << "Toggle Aimbot with F3\n";
	cout << "Toggle Bunnyhop with F4\n";

	hwnd = FindWindowA(NULL, "Counter-Strike: Global Offensive");
	GetWindowThreadProcessId(hwnd, &procId);
	moduleBase = GetModuleBaseAddress("client.dll");
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, NULL, procId);
	uintptr_t buffer;
	hdc = GetDC(hwnd);
	CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)FindClosestEnemyThread, NULL, NULL, NULL);

	
	while (!GetAsyncKeyState(VK_ADD))
	{

		//all toggles
		if (GetAsyncKeyState(VK_F1)) {
			toggle_wallhack = !toggle_wallhack;
			Sleep(200);
		}
		if (GetAsyncKeyState(VK_F2)) {
			toggle_triggerbot = !toggle_triggerbot;
			Sleep(200);
		}
		if (GetAsyncKeyState(VK_F3)) {
			toggle_aimbot = !toggle_aimbot;
			Sleep(200);
		}
		if (GetAsyncKeyState(VK_F4)) {
			toggle_bunnyhop = !toggle_bunnyhop;
			Sleep(200);
		}


		vm = RPM<view_matrix_t>(moduleBase + dwViewMatrix);
		Vector3 closestw2shead = WorldToScreen(get_head(GetPlayer(closest)), vm);

		//aimbot
		if (toggle_aimbot == true) {
			if (GetAsyncKeyState(VK_MENU /*alt key*/)) {
				SetCursorPos(closestw2shead.x, closestw2shead.y); //turn off "raw input" in CSGO 
			}
		}

		//triggerbot
		if (toggle_triggerbot == true) {
			int CrosshairID = getCrosshairID(GetLocalPlayer());
			int CrosshairTeam = getTeam(GetPlayer(CrosshairID - 1));
			int LocalTeam = getTeam(GetLocalPlayer());
			if (CrosshairID > 0 && CrosshairID < 32 && LocalTeam != CrosshairTeam && triggerbot_triggered == false)
			{
				if (GetAsyncKeyState(VK_MENU)) {
					Sleep(45);
				}
				mouse_event(MOUSEEVENTF_LEFTDOWN, NULL, NULL, 1, 1); 
				mouse_event(MOUSEEVENTF_LEFTUP, NULL, NULL, 0, 0);
				
				Sleep(155);
			}
		}

		//glow-wallhack 
		if (toggle_wallhack == true) {

			uintptr_t dwGlowManager = RPM<uintptr_t>(moduleBase + dwGlowObjectManager);
			int LocalTeam = RPM<int>(GetLocalPlayer() + m_iTeamNum);
			for (int i = 1; i < 32; i++) {
				uintptr_t dwEntity = RPM<uintptr_t>(moduleBase + dwEntityList + i * 0x10);
				int iGlowIndx = RPM<int>(dwEntity + m_iGlowIndex);
				int EnmHealth = RPM<int>(dwEntity + m_iHealth); if (EnmHealth < 1 || EnmHealth > 100) continue;
				int Dormant = RPM<int>(dwEntity + m_bDormant); if (Dormant) continue;
				int EntityTeam = RPM<int>(dwEntity + m_iTeamNum);

				if (LocalTeam == EntityTeam)
				{
					WPM<glowStructLocal>(dwGlowManager + (iGlowIndx * 0x38) + 0x4, glowLocal);
				}
				else if (LocalTeam != EntityTeam)
				{
					WPM<glowStructEnemy>(dwGlowManager + (iGlowIndx * 0x38) + 0x4, glowEnm);
				}
			}
		}

			//bunnyhop
			if (toggle_bunnyhop == true) {
				uintptr_t localPlayer = RPM<uintptr_t>(moduleBase + dwEntityList);
				int flags = RPM<int>(localPlayer + m_fFlags);
				if (flags & 1) {
					buffer = 5;
				}
				else {
					buffer = 4;
				}

				if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
					WPM(moduleBase + dwForceJump, buffer);
				}
			}
		}
	}
