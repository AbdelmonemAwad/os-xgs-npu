---
title: Cómo se mide
source: docs/measurements/methods.md
source_reviewed: 2026-10-21
---

# Cómo se toman las mediciones

*Traducción de la página [How the measurements are taken](/docs/measurements/methods/). Si difieren,
vale el texto en inglés.*

Cada página de medición dice cómo se tomaron sus números y qué no significan. Esta página reúne lo que
todas tienen en común: el banco de pruebas, dónde se lee una cifra, las herramientas, el cálculo detrás
de cada tipo de cifra y las reglas que cumple cada página - cada una con la razón de hacerlo así, porque
una cifra sin su método no prueba nada, y las que mejor aspecto tienen suelen medir otra cosa que la que
dicen.

## El banco de pruebas

Dos XGS 3300 y un XGS 136, enrutados entre sí, y un PC en el del medio a 10 Gbit/s - como en
[la página del banco](/docs/measurements/bench-2026-10-17/) (en inglés), donde se fotografía la ventana
de cada ejecución:

| enlace | desde | hasta | medio |
|---|---|---|---|
| el PC | la tarjeta 10GBASE-T del PC | F1 del segundo XGS 3300 | cobre, 10 Gbit/s |
| la fibra | el puerto de módulo A1 del segundo XGS 3300 | F1 del primer XGS 3300 | fibra, 10 Gbit/s |
| el enlace gigabit | Port8 del segundo XGS 3300 | Port8 del XGS 136 | cobre, 1 Gbit/s |

| qué | por qué |
|---|---|
| **Un par de hardware en ambos extremos de cada túnel.** | Hasta el 17-10-2026 el extremo lejano de un túnel medido era un firewall que hacía ESP en software, así que una cifra podía ser la de ese firewall y no la del equipo. |
| **Cada enlace con su propia /30, fuera del bridge de la LAN, con una regla de paso y una ruta estática.** | El tráfico se enruta a través del equipo en lugar de conmutarse a su lado, y nada más en la red comparte el camino. |
| **Nunca un servidor de pruebas público como extremo lejano.** | Son servidores de otras personas y rechazan lo que parece una inundación; una línea rechazada dejó una vez tres «descargas» en una tabla que nunca se habían ejecutado. Todo lo paralelo o repetido va a un extremo lejano del banco. |

## Dónde se lee una cifra

**En el receptor, en el extremo lejano - nunca en los contadores del propio equipo.** Una conexión en
hardware va de puerto a puerto dentro del coprocesador y nunca llega al host: el estado de pf, los
contadores de las interfaces y todos los gráficos construidos sobre ellos dejan de crecer. Un indicador que
marca cero cuando todo funciona no es un indicador - una ejecución la detuvo una vez una regla que leyó ese
cero como un colapso que nunca ocurrió.

**La cifra del receptor, no la del emisor.** iperf3 imprime ambas: el emisor cuenta lo que escribió en su
socket, el receptor lo que llegó. Solo la segunda es caudal.

**Una fila se lee solo cuando se sabe que se ejecutó** - bytes recibidos, no solo contadores que se
movieron.

## Las herramientas

| herramienta | qué mide aquí | por qué esta |
|---|---|---|
| **iperf3** - 3.20 en WSL en el PC para enviar, iperf3 para Windows para recibir, 3.22 (el paquete de OPNsense) como servidor en el host de un equipo | caudal, un flujo TCP o cuatro, diez segundos por ejecución | la misma herramienta en ambos extremos de cada ejecución, con su versión indicada. Dos clientes en el PC porque la red en espejo de WSL deja salir una trama de más de 1500 bytes pero no entrar: cada ejecución en la que el PC recibió más de 1500 midió 0,00 bits/s hasta que se volvió a medir con la versión de Windows |
| **curl sobre HTTPS, y SHA-256** | las pruebas de integridad prolongadas: un archivo de 1 GiB de bytes aleatorios descargado en bucle durante horas | TLS rechaza cualquier registro que no se autentique, y el hash del archivo frente al del servidor detecta lo que haya pasado entero; el caudal solo no dice nada de si los bytes eran correctos |
| **ping con el bit de no fragmentar, en tamaños fijos** | si una trama de cierto tamaño atraviesa un camino, y los tiempos de ida y vuelta | la prueba más sencilla de la MTU de un camino y de un puerto que recibe en absoluto |
| **sysctl** | las CPU (`kern.cp_time`), los contadores propios de los controladores (`dev.octep.0.*`, `dev.npuep.0.*`) y la caché de hosts TCP vaciada (`net.inet.tcp.hostcache.purgenow`) | los números del propio kernel, leídos igual en cada ejecución |
| **pfctl** | los estados de pf y sus recuentos de paquetes y bytes | lo que vio el firewall del host, para ponerlo junto a lo que vio el hardware |
| **las estadísticas propias del coprocesador**, leídas a través del controlador | lo que reenvió el hardware: tramas desde el cable, tramas que devolvió él mismo al cable, su recuento de cada conexión | el único testigo del tráfico que el host ya no ve |
| **mvsw** | los registros del chip del switch: el límite de trama de un puerto, sus contadores de errores | errores y límites que solo ve el switch, leídos de sus propios registros |
| **shot-run.ps1** | ejecuta cada comando de medición en su propia ventana de consola y fotografía esa ventana al terminar, con los píxeles de la propia ventana | la prueba es la pantalla tal como estaba, no una versión del texto reescrita después, y nada más del escritorio entra en la imagen |
| **tools/mark-images.py** | pone el propietario y la licencia (CC BY 4.0) en una franja bajo cada fotografía; el original sin marcar se guarda aparte | una imagen que sale de la página sigue diciendo de quién es |
| **tools/readme-art.py** | calcula el avance de cada equipo a partir de la tabla de la página de su familia | véase el cálculo más abajo; las comprobaciones fallan si una página dice otra cosa |

Los scripts que dirigen cada ejecución se nombran en la página de esa ejecución.

## El cálculo

| cifra | cómo se calcula | por qué |
|---|---|---|
| **caudal, una ejecución** | los bits por segundo del receptor durante la ejecución, tal como los imprime iperf3 | el recuento del receptor es lo que llegó |
| **caudal, una prueba prolongada** | bytes recibidos en las pasadas que tuvieron éxito × 8, entre los segundos desde el inicio de la primera pasada hasta el final de la última | una media de horas, sin contar los fallos como entregados |
| **ocupación de CPU** | `kern.cp_time` leído antes y después de la ejecución; ocupación = (total − reposo) ÷ total, de las diferencias | lo que costó la ejecución en toda su duración, donde un vistazo a `top` toma un solo instante |
| **proporción de tramas en hardware** | de las tramas que el coprocesador recibió del cable durante la ejecución (`RX_WIRE`), la proporción que devolvió él mismo al cable (`FROM_WIRE_TO_WIRE`): 6.630.528 de 6.657.897 es el 99,6 % | contadas por el hardware que las reenvió, no deducidas del silencio del host |
| **un contador, fiable** | contrastado con una cantidad conocida de antemano: el PC envía exactamente 4.000.000.000 bytes, y el recuento del coprocesador y el de pf juntos deben sumar lo que cruzó el cable - sumaron eso con una diferencia de 14 paquetes en 2,74 millones | un contador solo es fiable después de coincidir con algo contado de forma independiente; así se descubrió que dos estaban mal |
| **el resultado de una prueba de integridad** | pasadas que fallaron (la salida de curl), pasadas cuyo archivo difería (el hash) y bytes | una prueba solo se supera si ambos son cero |
| **el avance de un equipo** | cada fila de la tabla de la página de su familia tiene un porcentaje hecho y un peso; avance = Σ(hecho × peso) ÷ N, donde los pesos suman N y un valor tachado no cuenta | un porcentaje que no puede alejarse de lo que pondera: `tools/readme-art.py` lo calcula, y las comprobaciones fallan si la página o el README dicen otro |

## Las reglas que cumple cada página

| regla | por qué |
|---|---|
| **Cómo se tomó, y qué no significa**, en cada página | los límites de una cifra forman parte de ella: un flujo que termina en el propio host de un equipo mide lo menor entre el PC, el reenvío, el enlace y la ruta de recepción de ese host - no el techo del reenvío, que necesita una máquina detrás del equipo lejano |
| **Se nombra la compilación** - el commit del controlador o su suma de comprobación | un número pertenece al código que lo produjo |
| **Se fotografía cada ventana** cuando termina su ejecución | cualquiera puede ver la ejecución, no una copia de sus números; las direcciones del laboratorio se dejan en las imágenes, los números de serie y las direcciones de hardware se difuminan |
| **Lo que un host ha aprendido se olvida antes de cada ejecución** - su caché de hosts TCP y la caché de MTU de ruta del PC | una MTU de ruta aprendida en una ejecución pasó a la siguiente: una pasada entera se hizo con segmentos de 500 bytes y se descartó |
| **Lo que salió mal al medir se deja por escrito** | los errores - una MTU aprendida, las tramas perdidas de WSL - explican por qué una página midió como midió, y la siguiente medición los evita |
| **Una cifra retirada se tacha, no se borra**, con la corrección al lado | el lector ve qué cambió, y por qué |
| **Las cifras del fabricante se etiquetan como del fabricante**, junto a las nuestras | proceden de Keysight-Ixia BreakingPoint en condiciones ideales, sin indicar la MTU, los puertos ni si cuentan ambos sentidos: no son el mismo tipo de número que uno tomado aquí |

## Dónde están los números

Cada medición, por equipo y por día: [las mediciones](/docs/measurements/) (en inglés).
