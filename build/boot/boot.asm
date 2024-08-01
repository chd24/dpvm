; DPVM boot loader, called from loader.bin;
; fasm

	org	100h
	use16

CPU_N_IO_PORTS = 0FF00h

	jmp	start
	db	' '
version:
	db	'DPVM boot loader T10.520-T20.365' ; $DVS:time$
	db	0


; ====================== ;
; ПРИВЕТСТВИЕ И ПРОВЕРКИ ;
; ====================== ;

start:	
	cld
	mov	ax, cs
	mov	bx, STACK_end
	mov	ss, ax
	mov	sp, bx
	mov	ds, ax
	mov	es, ax

; вывод версии
	mov	ah, 0Fh	; узнать номер видеорежима
	int	10h
	cmp	al, 3
	je	mode3
	mov	ax, 3	; установить 3-й видеорежим
	int	10h
mode3:
	mov	si, version
	call	outStrLn2consol

; проверка, не находимся ли мы в virtual 8086 режиме
	smsw	ax
	test	al, 1
	jz	in_real_mode
	mov	si, vm8086
	call	outStrLn2consol
	jmp	fin

vm8086:	db	'CPU in VM8086 mode.', 0

; проверка, поддерживается ли длинный режим
in_real_mode:
	mov	eax, 80000000h
	cpuid
	cmp	eax, 80000000h
	jbe	no_long_mode
	mov	eax, 80000001h
	cpuid
	bt	edx, 29
	jc	init_data
no_long_mode:
	mov	si, no_64
	call	outStrLn2consol
	jmp	fin

no_64:	db	'AMD64 not supported.', 0


; ==================== ;
; ИНИЦИАЛИЗАЦИЯ ДАННЫХ ;
; ==================== ;

init_data:

; установка битов PORT_MAP в 1 (если бит равен 0, то доступ к порту открыт), очистка IDT
	xor	eax, eax
	dec	eax
	mov	di, PORT_MAP
	mov	cx, (PORT_MAP_end - PORT_MAP) / 4 + 1
	rep	stosd
	inc	eax
	mov	cx, (IDT_end - PORT_MAP_end) / 4 - 1
	rep	stosd

; установка адреса текущего сегмента в различные таблицы и команды
; в esi будет храниться физический адрес начала данного сегмента
	xor	esi, esi
	mov	si, cs
	mov	[real_mode_back - 2], si
	shl	esi, 4
	add	[CODE16 + 2], esi
	add	[DATA16 + 2], esi
	add	[TSS64 + 2], esi
	add	[LDT64 + 2], esi
	add	[TSS + 4], esi
	add	[pGDT32], esi
	add	[pIDT64], esi
	add	[long_mode - 6], esi
	mov	[IDT_begin], word IDT
	mov	[GDT_begin], word GDT
	mov	[GDT_end_a], word GDT_end
	mov	[TSS_begin], word TSS
	mov	[TSS_end_a], word TSS_end
	add	[IDT_begin], esi
	add	[GDT_begin], esi
	add	[GDT_end_a], esi
	add	[TSS_begin], esi
	add	[TSS_end_a], esi
	mov	[code64_kernel], word CODE64S - GDT
	mov	[code64_user],	 word CODE64U - GDT + 3
	mov	[data64_kernel], word DATA64S - GDT
	mov	[data64_user],   word DATA64U - GDT + 3
	mov	[tss64],         word TSS64   - GDT
	mov	[ldt64],         word LDT64   - GDT

; формирование IDT для 64-битного режима
	mov	di, IDT
	mov	cx, 256
	lea	edx, [esi + interrupt_catch]
	mov	eax, edx
	shr	eax, 16
idtcyc:	mov	[di], dx
	mov	[di + 2], word (CODE64S - GDT)
	mov	[di + 4], word 8E00h
	mov	[di + 6], ax
	add	di, 16
	loop	idtcyc

; формирование карты физической памяти
	mov	edi, MMAP
	xor	ebx, ebx
	mov	[mem_map_begin], edi
	add	[mem_map_begin], esi
	mov	ebp, 0xA0000		; конец нижней RAM по умолчанию
mmcyc:	mov	ecx, 24
	mov	[di + 20], dword 0BEDA0BEDh
	mov	edx, 534D4150h		; 'SMAP'
	mov	eax, 0E820h
	int	15h
	jc	mmend
	cmp	eax, 534D4150h
	jne	mmend
	cmp	[di], dword 0
	jne	mmnxt
	cmp	[di + 4], dword 0
	jne	mmnxt
	mov	ebp, [di + 8]
mmnxt:	add	di, 24
	or	ebx, ebx
	jnz	mmcyc
mmend:	add	edi, esi
	and	bp, 0F000h
	mov	[mem_map_end], edi
	mov	[free_mem_end], ebp

; формирование таблицы страниц
	lea	ebx, [edi + 0FFFh]
	and	bx, 0F000h
	mov	[root_page], ebx
	mov	edi, ebx
	sub	edi, esi
; очистка 4-х страниц
	mov	dx, di
	xor	eax, eax
	mov	ecx, 1000h
	rep	stosd
; заполнение 0-го поля первых 3-х страниц
	mov	di, dx
	lea	eax, [ebx + 1000h + 7]	; 7 - present, writable, user
	mov	[di], eax
	add	eax, 1000h
	mov	[di + 1000h], eax
	add	eax, 1000h
	mov	[di + 2000h], eax
; заполнение половины 4-й страницы - указатели на 1-й мегабайт памяти
	xor	eax, eax
	mov	al, 3			; 3 - present, writable, system
	add	di, 3000h
	mov	cx, 256
pcyc:	mov	[di], eax
	add	eax, 1000h
	add	di, 8
	loop	pcyc
	add	ebx, 4000h
	mov	[free_mem_begin], ebx

; ============= ;
; ЗАГРУЗКА ЯДРА ;
; ============= ;

; чтение имени файла ядра из командной строки
	sub	sp, 80h
	mov	si, 81h
	mov	di, sp
	xor	cl, cl
	call	getNextParameter
	cmp	cl, 0
	jne	filename_ok
	mov	si, no_arg
	call	outStrLn2consol
	jmp	fin

no_arg:	db	'File not given.', 0

; открытие файла ядра
filename_ok:
	mov	dx, sp
	mov	ah, 3Dh
	mov	al, 0
	int	21h
	jnc	file_opened
	mov	si, nokern
	call	outStr2consol
	mov	si, sp
	call	outStrLn2consol
	jmp	fin

nokern:	db	'Error open file ', 0

read_fail:
	mov	si, rfail
	call	outStr2consol
	mov	si, sp
	call	outStrLn2consol
	jmp	fin

rfail:	db	'Error read file ', 0

; загрузка ядра порциями по 4096 байт после индексных страниц
file_opened:
	mov	edi, [free_mem_begin]
	mov	esi, [free_mem_end]
	mov	[dpvm_begin], edi
	shr	esi, 4
	shr	edi, 4
	mov	bx, ax
rcyc:	xor	dx, dx
	mov	cx, 1000h
	mov	ah, 3Fh
	push	ds
	mov	ds, di
	int	21h
	pop	ds
	jc	read_fail
	add	di, 100h
	cmp	ax, 1000h
	jne	read_ok
	cmp	di, si
	jb	rcyc
	mov	si, toobig
	call	outStr2consol
	mov	si, sp
	call	outStrLn2consol
	jmp	fin

toobig:	db	'Too large file ', 0

kread:	db	'File loaded to ', 0

kread1: db	': ', 0

; закрытие файла ядра
read_ok:
	mov	ah, 3Eh
	int	21h
	mov	si, kread
	call	outStr2consol
	mov	eax, [dpvm_begin]
	mov	cl, 8
	call	outHexNum2consol
	mov	si, kread1
	call	outStr2consol
	mov	si, sp
	call	outStrLn2consol
	add	sp, 80h
	shl	edi, 4
	mov	[dpvm_end], edi
	mov	[free_mem_begin], edi

; изменение начала сегмента для последующего запуска других процессоров
	xor	eax, eax
	mov	ax, ss
	mov	[0], byte 0xfa		; cli
	mov	[1], byte 0xea		; jmp far
	mov	[2], word AP_init
	mov	[4], ax
	shl	eax, 4
	mov	[AP_startup], eax

; ========================= ;
; ПЕРЕХОД В 64-БИТНЫЙ РЕЖИМ ;
; ========================= ;

; загрузка GDT, переход в защищённый режим
	call	a20enable
	cli
goto_long_mode:
	push	dword 0
	popfd
	lgdt	[pGDT]
	mov	eax, cr0
	bts	eax, 0
	and	eax, 0x9fffffff
	mov	cr0, eax
	jmp	dword (CODE16 - GDT):prot_mode

; в защищённом режиме, 16-битном коде
prot_mode:
	mov	ax, DATA16 - GDT
	mov	ss, ax
	mov	esp, STACK_end
	mov	ds, ax
	mov	es, ax

; включение PAE, загрузка базы дерева страниц
	mov	eax, cr4
	bts	eax, 5
	mov	cr4, eax
	mov	ebx, [root_page]
	mov	cr3, ebx

; переход в 64-битный режим, включение страничности
	mov	ecx, 0C0000080h
	rdmsr
	bts	eax, 8
	wrmsr
	mov	eax, cr0
	bts	eax, 31
	mov	cr0, eax
	jmp	pword (CODE64S - GDT):long_mode


; =============== ;
; 64-БИТНЫЙ РЕЖИМ ;
; =============== ;

	use64

; в 64-битном режиме 
long_mode:
	lea	esi, [rip - next1]
next1:
	lea	esp, [rsi + STACK_end]
	lidt	[rsi + pIDT]
	mov	eax, 1
	cpuid
	shr	ebx, 19
	and	bx, 0x1FE0
	mov	ax, bx
	add	ax, TSS64 - GDT
	ltr	ax
	xor	ax, ax
	lldt	ax

; переход к выполнению ядра
	lea	edi, [rsi + INIDATA]
	push	rdi
	mov	rbx, [rsi + dpvm_begin]
	call	rbx

; ядро завершилось с кодом ошибки
	push	rax
	mov	di, 0
	jmp	goto_prot_mode

; точка входа прерываний
interrupt_catch:
	or	sp, 8
	mov	di, 1

; ===================================== ;
; ВОЗВРАТ В РЕАЛЬНЫЙ РЕЖИМ И ЗАВЕРШЕНИЕ ;
; ===================================== ;

; возвращение в защищённый режим:
goto_prot_mode:
	cli
	jmp	far dword [rip - code16_back + pTo16]

	use16

; снова в 16-разрядном сегменте
code16_back:
	mov	eax, cr0
	btc	eax, 31
	mov	cr0, eax
	jmp	dword (CODE16 - GDT):paging_disabled

; снова в защищённом режиме
paging_disabled:
	mov	ecx, 0C0000080h
	rdmsr
	btc	eax, 8
	wrmsr
	mov	ax, DATA16 - GDT
	mov	ss, ax
	mov	ds, ax
	mov	es, ax
	mov	eax, cr4
	btc	eax, 5
	mov	cr4, eax
	
; выход в реальный режим
	mov	eax, cr0
	btc	eax, 0
	mov	cr0, eax
	jmp	dword 0:real_mode_back

; в реальном режиме
real_mode_back:
	xor	eax, eax
	mov	ax, cs
	mov	ss, ax
	mov	ds, ax
	mov	es, ax
	lgdt	[pIDTold]
	lidt	[pIDTold]
	shl	eax, 4
	sub	esp, eax

; завершение
	mov	si, mfail
	call	outStrLn2consol
	or	di, di
	jnz	exept_occured
	mov	si, merror
	jmp	out_fail_mess

exept_occured:
	mov	si, mexept

out_fail_mess:
	call	outStr2consol
	mov	cl, 8
	mov	eax, [esp + 4]
	call	outHexNum2consol
	mov	eax, [esp]
	call	outHexNum2consol
	mov	si, mfaile
	call	outStrLn2consol

fin:
	sti
fincyc:
	xor	ecx,ecx
	dec	ecx
	loopd	fincyc	
	int	20h
	
mfail:	db	'DPVM finished.', 0
merror:	db	'DPVM error 0x', 0
mexept:	db	'Exception, rip=0x', 0
mfaile:	db	'.', 0


; ======================= ;
; ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ;
; ======================= ;

outSymb2consol:
; выводит символ al на консоль
	pusha
	mov	ah, 0Eh
	mov	bl, 7
	int	10h
	popa
	ret	

outStr2consol:
; выводит строку [si] на консоль
	push	ax
	push	si
oStrC1:	lodsb		; не меняет флагов
	or	al, al
	jz	oStrC2
	call	outSymb2consol
	jmp	oStrC1
oStrC2:	pop	si
	pop	ax
	ret

outStrLn2consol:
; выводит строку [si] и перевод строки на консоль
	call	outStr2consol
	push	si
	mov	si, newlin
	call	outStr2consol
	pop	si
	ret

newlin:	db	13, 10, 0

outHexNum2consol:
; выводит cl младших 16-ричных цифр из eax
	pushad
	mov	edx, eax
	shl	cl, 2
oHNC1:	jz	oHNC2
	mov	ebx, edx
	sub	cl, 4
	shr	ebx, cl
	and	bx, 0Fh
	mov	al, [bx + oHNCsym]
	call	outSymb2consol
	or	cl, cl
	jnz	oHNC1
oHNC2:	popad
	ret

oHNCsym:db	'0123456789ABCDEF'

getNextParameter:
; читает следующий аргумент из командной строки
; ds:si - текущее положение в ком. строке
; cl - счётчик числа аргументов
; es:di - куда сохранить строку + 0
; изменяет cl,si,di
	push	ax
nxtP_1:	lodsb
	cmp	al, ' '
	je	nxtP_1
	cmp	al, 9 ; '\t'
	je	nxtP_1
	cmp	al, 0Dh
	je	nxtP_3
	inc	cl
nxtP_2:	stosb
	lodsb
	cmp	al, ' '
	je	nxtP_4
	cmp	al, 9
	je	nxtP_4
	cmp	al, 0Dh
	jne	nxtP_2
nxtP_3:	dec	si
nxtP_4: xor	al, al
	stosb
	pop	ax
	ret

a20wait:
; задержка, пока порт 64h занят
	in	al, 64h
	test	al, 2
	jnz	a20wait
	ret

a20wait2:
; задержка, пока порт 60h занят
	in	al, 64h
	test	al, 1
	jz	a20wait2
	ret

a20enable:
; включает 20-й бит адресной шины (пытается это делать)
	push	ax
	mov	ax, 2401h
	int	15h		; пробует через BIOS
	jnc	a20ret
	cli
	call	a20wait
	mov	al, 0ADh
	out	64h, al
	call	a20wait
	mov	al, 0D0h
	out	64h, al
	call	a20wait2
	in	al, 60h
	push	ax
	call	a20wait
	mov	al, 0D1h
	out	64h, al
	call	a20wait
	pop	ax
	or	al, 2
	out	60h, al
	call	a20wait
	mov	al, 0AEh
	out	64h, al
	call	a20wait
	sti
a20ret:	pop	ax
	ret

; инициализация второго и следующего процессоров
AP_init:
	cld
	xor	eax, eax
	mov	ax, cs
	mov	bx, STACK_end
	mov	ss, ax
	mov	sp, bx
	mov	ds, ax
	mov	es, ax
	mov	ebx, 0x15000000
	mov	eax, 0xfffff010
	mov	[dpvm_begin], ebx
	mov	[dpvm_begin + 4], eax
	jmp	goto_long_mode


; ================== ;
; СТАТИЧЕСКИЕ ДАННЫЕ ;
; ================== ;

	align	16

pTo16:	dw	code16_back, CODE16 - GDT
	rb	2

; данные для 64-х разрядной команды lidt
pIDT:
	dw	IDT_end - IDT - 1
pIDT64:	dq	IDT
	rb	2

; данные таблицы IDT реального режима
pIDTold:
	dw	0FFFFh
	dd	0
	rb	2

; данные для 32-х разрядной команды lgdt
pGDT:
	dw	GDT_end - GDT - 1
pGDT32:	dd	GDT

; глобальная таблица дескрипторов
GDT:
; null descriptor
	rb	16
; 16-разрядный сегмент кода (текущий сегмент)
CODE16:
	dw	0FFFFh
	dw	0
	db	0, 9Ah, 0, 0
; 16-разрядный сегмент данных (текущий сегмент)
DATA16:
	dw	0FFFFh
	dw	0
	db	0, 92h, 0, 0
; 64-разрядный сегмент кода DPL = 0 (вся память)
CODE64S:
	dw	0, 0
	db	0, 9Ah, 20h, 0
; 64-разрядный сегмент кода DPL = 3 (вся память)
CODE64U:
	dw	0, 0
	db	0, 0FAh, 20h, 0
; 64-разрядный сегмент данных DPL = 0 (вся память)
DATA64S:
	dw	0, 0
	db	0, 092h, 0, 0
; 64-разрядный сегмент данных DPL = 3 (вся память)
DATA64U:
	dw	0, 0
	db	0, 0F2h, 0, 0
; 64-разрядный сегмент задачи, CPU0
TSS64:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU0
LDT64:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
; 64-разрядный сегмент задачи, CPU1
TSS64_1:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU1
LDT64_1:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
; 64-разрядный сегмент задачи, CPU2
TSS64_2:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU2
LDT64_2:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
; 64-разрядный сегмент задачи, CPU3
TSS64_3:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU3
LDT64_3:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
; 64-разрядный сегмент задачи, CPU4
TSS64_4:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU4
LDT64_4:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
; 64-разрядный сегмент задачи, CPU5
TSS64_5:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU5
LDT64_5:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
; 64-разрядный сегмент задачи, CPU6
TSS64_6:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU6
LDT64_6:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
; 64-разрядный сегмент задачи, CPU7
TSS64_7:
	dw	TSS_end - TSS - 1
	dw	TSS
	db	0, 89h, 0, 0
	dd	0, 0
; 64-разрядный сегмент LDT, CPU7
LDT64_7:
	dw	7
	dw	GDT
	db	0, 82h, 0, 0
	dd	0, 0
GDT_end:

; сегмент задачи
TSS:
	dd	0
	dd	STACK_end
	dd	0
	rd	22
	dw	0, PORT_MAP - TSS
PORT_MAP:
	rb	CPU_N_IO_PORTS / 8
PORT_MAP_end:
	rb	1
TSS_end:

	align	8

; данные, передаваемые приложению dpvm
INIDATA:
dpvm_begin:	rq	1 ; начало загруженной программы в памяти
dpvm_end:	rq	1 ; её конец
free_mem_begin:	rq	1 ; начало свободной нижней памяти
free_mem_end:	rq	1 ; её конец
mem_map_begin:	rq	1 ; начало карты памяти
mem_map_end:	rq	1 ; конец карты памяти (каждая запись имеет длину 24 байта)
root_page:	rq	1 ; корневой каталог страниц
IDT_begin:	rq	1 ; адрес IDT
GDT_begin:	rq	1 ; адрес GDT
GDT_end_a:	rq	1 ; конец GDT
TSS_begin:	rq	1 ; адрес TSS
TSS_end_a:	rq	1 ; конец TSS
code64_kernel:	rw	1 ; дескриптор 64-разрядного сегмента кода для kernel-mode
code64_user:	rw	1 ; дескриптор 64-разрядного сегмента кода для user-mode
data64_kernel:	rw	1 ; дескриптор 64-разрядного сегмента данных для kernel-mode
data64_user:	rw	1 ; дескриптор 64-разрядного сегмента данных для user-mode
tss64:		rw	1 ; дескриптор 64-радрядного сегмента TSS
ldt64:		rw	1 ; дескриптор 64-радрядного сегмента LDT
AP_startup:	rd	1 ; физический адрес стартовой процедуры для остальных процессоров
INIDATA_end:
	
	align	16

; таблица дескрипторов прерываний для 64-разрядного режима
IDT:
	rb	1000h
IDT_end:

; место под стек
STACK_begin:
	rb	8000h
STACK_end:

; начало карты памяти (конец неопределён)
MMAP:

; далее после выравнивания по границе 1000h будут идти индексные страницы
