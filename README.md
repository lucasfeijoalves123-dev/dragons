# DragoesGrandes (SKSE) - Skyrim SE/AE 1.6.1170

Todos os dragoes 15% maiores + brilho leve na cor do elemento (fogo/gelo/choque).

## Requisitos no jogo
- SKSE64 2.2.6 (para 1.6.1170)
- Address Library for SKSE Plugins (versao AE, com 1.6.1170)

## Gerar o mod para o Vortex (sem instalar nada no PC)
1. Crie um repositorio no GitHub e suba o conteudo desta pasta (incluindo `.github/`).
2. Aba **Actions** > workflow **build** > **Run workflow**. Leva alguns minutos.
3. Abra a execucao concluida e baixe o artefato **DragoesGrandes-1.0.0** (e um .zip).
4. Esse .zip ja esta no formato do Vortex (`SKSE/Plugins/` na raiz). Arraste-o para o Vortex,
   instale, ative e faca Deploy.

Dentro do zip ficam:
  SKSE/Plugins/DragoesGrandes.dll
  SKSE/Plugins/DragoesGrandes.ini

## Depois de instalar
- Inicie o jogo pelo `skse64_loader.exe` (no Vortex, a ferramenta SKSE64).
- Log: Documents/My Games/Skyrim Special Edition/SKSE/DragoesGrandes.log
  (mostra a config lida, os shaders criados e em quais dragoes o brilho foi aplicado).

## Ajustes
Tudo no `DragoesGrandes.ini` (Vortex: abra a pasta do mod > SKSE/Plugins): escala,
intensidade do brilho, cores e nomes por elemento. Reinicie o jogo depois de editar.

## Compilar localmente (opcional)
Visual Studio 2022 + vcpkg:
  git init extern/CommonLibSSE-NG
  git -C extern/CommonLibSSE-NG remote add origin https://github.com/CharmedBaryon/CommonLibSSE-NG
  git -C extern/CommonLibSSE-NG fetch --depth 1 origin b93280e832f263dbef44e44cbe2936622a02f91a
  git -C extern/CommonLibSSE-NG checkout FETCH_HEAD
  cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
  cmake --build build --config Release
A DLL sai em build/Release/DragoesGrandes.dll.
