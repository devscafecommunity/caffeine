# Caffeine contra o manual da Unity 5.6

Inventário do Doppio e do runtime em outubro de 2026, lido do código em `src/`. O critério é conseguir o trabalho que a página descreve, não copiar o nome do menu.

| Temos | Parcial | Falta | Fora de escopo |
| ---: | ---: | ---: | ---: |
| 19 | 32 | 17 | 6 |

## O que este plano não é

Famílias legacy de shaders, Asset Store, serviços online, IL2CPP e cluster não entram na fila. O caminho gráfico já é PBR forward com HDR. O scripting é Lua, não C#.

## Cobertura por capítulo

Contagem de entradas deste inventário. Fora de escopo não entra nas barras.

| Capítulo | Temos | Parcial | Falta |
| --- | ---: | ---: | ---: |
| Arranque | 1 | 1 | 0 |
| Assets | 1 | 2 | 1 |
| Janelas | 3 | 5 | 0 |
| Cena | 3 | 3 | 0 |
| Input e build | 0 | 2 | 2 |
| Editor | 1 | 1 | 0 |
| 2D | 1 | 2 | 2 |
| Luz e materiais | 3 | 4 | 4 |
| Terreno e efeitos | 1 | 2 | 1 |
| Física 3D | 2 | 1 | 1 |
| Scripting | 1 | 2 | 0 |
| UI | 1 | 1 | 2 |
| Navegação | 0 | 1 | 0 |
| Animação | 0 | 4 | 2 |
| Áudio | 0 | 1 | 1 |
| Rede e serviços | 0 | 0 | 1 |

## Ordem de construção

Para um editor em que se importa um personagem, se anima e se joga a cena. Não é a ordem do manual.

| | Frente | O que fecha |
| ---: | --- | --- |
| 1 | Personagem visível e estável | Recarregar o glTF com a calibração de bind, confirmar Running no sítio do gizmo e o frame time sem o mesh de 17 km. |
| 2 | Animation Window a sério | Playhead que fica onde se larga, Key no tempo certo, curvas de rotação por osso e replay do clipe gravado. |
| 3 | Animator com parâmetros | Transições por float/bool/trigger, não só um estado por nome de clipe. |
| 4 | Import Settings do modelo | Escala, eixo, clipes e rig numa ficha do asset, em vez de adivinhar na carga. |
| 5 | Física de personagem | Character controller, mesh collider e um joint. Cloth e wheel ficam depois. |
| 6 | UI de jogo utilizável | Imagem, texto editável, scroll e um layout horizontal/vertical. |
| 7 | NavMesh | Bake a partir da geometria e um agente que anda até um ponto, acoplado ao animator. |
| 8 | Definições de projeto | Um sítio para física, tempo, qualidade, tags e input, como os managers da Unity. |

## Lista do manual

74 entradas. Fonte: árvore `src/` do Caffeine, não a documentação da Unity. Páginas irmãs, como cada shader legacy ou cada joint 2D, contam como uma lacuna só.

| Capítulo | Página da Unity | Estado | No Caffeine |
| --- | --- | --- | --- |
| Arranque | Instalador e Learn tab | Fora de escopo | O Doppio compila por CMake. Não há hub nem tutoriais embutidos. |
| Arranque | Projeto 2D ou 3D | Temos | template_type no project.caffeine abre a viewport no modo certo. |
| Arranque | Deploy offline do editor | Parcial | Binário local, sem pacote de instalação nem ativação. |
| Assets | Primitivas (cubo, esfera, cilindro, cone) | Temos | Meshes procedurais partilhados, com geometria editável por entidade. |
| Assets | Importar modelos, texturas e materiais | Parcial | glTF/GLB/OBJ, PNG e .mat. FBX não é um importador de primeira classe. |
| Assets | Import Settings por asset | Falta | Sem aba de escala, rig, animações ou compressão na importação. |
| Assets | Asset Store e Standard Assets | Fora de escopo | Loja da Unity. O projeto recebe céus default em assets/raw/sky. |
| Assets | Pacotes de assets | Parcial | Prefabs e caf-pack. Não há .unitypackage. |
| Janelas | Project, Hierarchy, Inspector | Temos | Asset Browser, hierarquia e inspector de componentes. |
| Janelas | Scene View e navegação | Temos | Órbita, pan, zoom, Persp/Ortho, gizmo de eixos projetado. |
| Janelas | Mover, rodar e escalar | Temos | Gizmos no Toolbox, espaço local/mundo, snap. |
| Janelas | Barra da Scene View e menu Gizmos | Parcial | Toolbox no lugar da barra. Luzes e ossos desenham-se, sem menu para os ligar. |
| Janelas | Game View | Parcial | Gameplay Preview. Não é uma vista de jogo com resolução e stats próprias. |
| Janelas | Ícones, presets e pesquisa | Parcial | Presets de entidade e materiais. Sem ícones na hierarquia nem pesquisa global. |
| Janelas | Workspace e janelas do sistema | Parcial | Painéis saem para janela do SO. No Wayland o mapeamento ainda é frágil. |
| Janelas | Hotkeys do editor | Parcial | Ctrl+S grava, Ctrl+Z desfaz, T/E/R/Q no gizmo. Sem mapa completo. |
| Cena | Cenas, entidades e componentes | Temos | ECS com serializer do editor, undo por snapshot da cena. |
| Cena | Transform | Temos | 2D e 3D, posição, rotação e escala. |
| Cena | Desativar, tags e static | Parcial | Há disable efetivo. Tag existe vazio. Static e layers de render não existem. |
| Cena | Prefabs | Parcial | Gravar e instanciar. Sem overrides aninhados como na Unity. |
| Cena | Gravar a cena | Temos | Ctrl+S escreve a cena e os materiais. |
| Cena | Componentes por script | Parcial | Lua, não C#. Sem MonoBehaviour nem ordem de execução. |
| Input e build | Input de teclado, rato e gamepad | Parcial | Actions e eixos no código. Sem Input Manager editável. |
| Input e build | VR, OpenVR e input móvel | Falta | Sem dispositivos de VR nem teclado móvel. |
| Input e build | Build Settings e Player Settings | Parcial | Diálogo de build. Sem matriz de plataformas, splash ou qualidade por plataforma. |
| Input e build | Managers (física, tempo, tags, qualidade, gráficos) | Falta | Opções estão espalhadas. Não há ecrã de projeto equivalente. |
| Editor | Preferências e modo 2D/3D | Temos | Settings e viewport 2D, 3D e isométrica. AA do viewport é separado do jogo. |
| Editor | Pastas especiais e controlo de versões | Parcial | assets/raw e plugin de git. Sem Perforce nem export de package. |
| Editor | Visual Studio, RenderDoc, analytics, IME | Fora de escopo | Integrações do produto Unity. O editor tem consola e crash log próprios. |
| 2D | Sprites e sprite sheet | Parcial | Sprite, folha e clipe de frames. Sem Sprite Editor, outline, packer nem 9-slice. |
| 2D | Sorting Group | Falta | Sem ordenação de sprites por grupo. |
| 2D | Rigidbody 2D e colliders simples | Parcial | Corpo e collider caixa ou círculo. Sem polígono, aresta, cápsula nem composto. |
| 2D | Joints, effectors e material 2D | Falta | Nenhum dos joints nem effectors da lista. |
| 2D | Tilemap | Temos | Editor de tilemap, fora desta lista da Unity 5.6 mas já no Doppio. |
| Luz e materiais | Direcional, ponto e spot | Temos | Mais luz volumétrica como mesh editável. |
| Luz e materiais | Cookies, area light e Light Explorer | Falta | Sem textura de cookie nem janela de todas as luzes. |
| Luz e materiais | Sombras | Parcial | Sombra direcional no GPU. Sem shadowmask, baked nem luzes todas a projetar. |
| Luz e materiais | GI, lightmaps e light probes | Falta | Sem pré-cálculo. IBL difuso está desligado de propósito; especular reflete o céu. |
| Luz e materiais | Skybox de projeto | Temos | Assets em assets/raw/sky. Sem céu, o fallback é azul com horizonte branco. |
| Luz e materiais | Câmaras e frustum | Parcial | Câmara 2D/3D e pós na câmara. Sem várias câmaras, oblíquo nem occlusion culling. |
| Luz e materiais | Standard Shader / PBR | Temos | Albedo, metal, rugosidade, normal, AO, emissão, opacidade, transmissão, clearcoat, sheen. |
| Luz e materiais | Famílias legacy de shaders | Fora de escopo | Vertex-lit, bumped, parallax e reflective antigos. O caminho é PBR forward. |
| Luz e materiais | Reflection probes e SSR | Parcial | Probes automáticos por objeto e SSR. Sem probes colocados à mão. |
| Luz e materiais | HDR e linear | Temos | Cena HDR e texturas sRGB. Sem escolher gamma vs linear no projeto. |
| Luz e materiais | LOD, instancing e batching | Parcial | LOD de mesh e instancing parcial. Sem frame debugger nem stats de draw calls. |
| Luz e materiais | Compute, command buffers, sparse, cluster | Falta | O RHI tem command buffer interno. Nada disto está exposto ao jogo. |
| Luz e materiais | Vídeo | Falta | Sem Video Player. |
| Terreno e efeitos | Terreno: altura, tintas e LOD | Parcial | Esculpir, splat e chunks. Sem árvores, erva, vento nem SpeedTree. |
| Terreno e efeitos | Partículas | Parcial | Emissor simples e efeitos. Sem sistema modular da Unity. |
| Terreno e efeitos | Pós-processamento | Temos | AA, AO, SSR, fog, DOF, motion blur, exposure, bloom, grading, CA, grain, vigneta. |
| Terreno e efeitos | LUT, dithering, monitores e debug de pós | Falta | O stack existe. Faltam vistas de diagnóstico e LUT de utilizador. |
| Física 3D | Rigidbody e material simples | Temos | Dinâmico, cinemático, estático, fricção e restituição no corpo. |
| Física 3D | Box, sphere e capsule | Temos | Mais trigger. Sem mesh collider nem terrain collider dedicado na lista de componentes. |
| Física 3D | Joints, ragdoll, cloth e wheel | Falta | Nenhum joint, character controller, cloth nem roda. |
| Física 3D | Debug de física | Parcial | Overlay 2D. O 3D não tem a vista de debug da Unity. |
| Scripting | Scripts no inspector e eventos | Parcial | Lua com bindings. Sem corrotinas, UnityEvent nem serialização de campos de script. |
| Scripting | Consola | Temos | Console do editor e log de crash. |
| Scripting | Test runner e debugger | Parcial | Catch2 fora do editor. Sem debugger ligado ao jogo. |
| Scripting | IL2CPP e stripping | Fora de escopo | O runtime já é C++ nativo. |
| UI | Canvas, rect, botão, texto, slider, checkbox | Temos | UI retida com layout básico. |
| UI | Image, mask, dropdown, input, scroll e layout groups | Falta | Sem auto-layout, scaler, máscara nem campo de texto. |
| UI | UI de mundo e várias resoluções | Falta | Sem canvas world-space nem transições de ecrã. |
| UI | IMGUI | Parcial | O editor é ImGui. O jogo não expõe OnGUI. |
| Navegação | NavMesh, agente, obstáculo e links | Parcial | Grelha NavVolume e agente. Sem bake de malha, áreas, custos nem off-mesh. |
| Animação | Clipes importados de glTF | Parcial | Running/Walking entram. A escala cm/m do armature partia o mesh; a calibração de bind corrige isso na próxima carga. |
| Animação | Avatar humanoide | Parcial | Mapeamento automático e manual de ossos. Sem músculos nem avatar asset. |
| Animação | Animation Window | Parcial | Timeline com ossos e objeto. Keyframes ainda não são curvas editáveis, e o tempo do playhead não é fiável. |
| Animação | Animator: estados, parâmetros, transições | Parcial | Grafo de estados a partir dos clipes. Sem parâmetros, blend trees, layers, IK nem root motion. |
| Animação | Máscaras, eventos, split e retarget | Falta | Sem avatar mask, animation events nem retarget entre personagens. |
| Animação | Importador FBX de rig | Falta | Sem abas Rig e Animations da Unity. |
| Áudio | Clip e fonte | Parcial | AudioEmitter e preview. Sem listener como componente de cena completo. |
| Áudio | Mixer, filtros, zonas e microfone | Falta | Sem Audio Mixer nem reverb zones. |
| Rede e serviços | HLAPI, lobby, transport e serviços | Fora de escopo | Rede da Unity e Unity Services. Não são o núcleo do editor. |
| Rede e serviços | VR e cluster | Falta | Sem render em cluster nem câmaras de VR. |

## Leitura rápida

Já dá para trabalhar: cena, hierarquia, inspector, gizmos, materiais PBR, céu do projeto, pós-processamento, luzes, terreno básico, prefabs simples, física de primitivas e UI mínima.

Está pela metade e é o que se sente no editor: importação de personagens, timeline, animator, sombras, probes, partículas, navegação em grelha, áudio e as definições de projeto.
