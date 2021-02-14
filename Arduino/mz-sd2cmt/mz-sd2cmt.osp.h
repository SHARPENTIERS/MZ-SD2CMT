#pragma once

template<int TIMER = -1> struct OneShortPulseTraits;

template<> struct OneShortPulseTraits<-1>
{
	// TCCRnA:
	static constexpr auto COMnA1 = 7;
	static constexpr auto COMnA0 = 6;
	static constexpr auto COMnB1 = 5;
	static constexpr auto COMnB0 = 4;
	static constexpr auto COMnC1 = 3;
	static constexpr auto COMnC0 = 2;
	static constexpr auto WGMn1  = 1;
	static constexpr auto WGMn0  = 0;

	// TCCRnB:
	static constexpr auto ICNCn = 7;
	static constexpr auto ICESn = 6;
	static constexpr auto WGMn3 = 4;
	static constexpr auto WGMn2 = 3;
	static constexpr auto CSn2  = 2;
	static constexpr auto CSn1  = 1;
	static constexpr auto CSn0  = 0;

	// TCCRnB:
	static constexpr auto FOCnA = 7;
	static constexpr auto FOCnB = 6;
	static constexpr auto FOCnC = 5;
};

#if 0
template<> struct OneShortPulseTraits<1> : OneShortPulseTraits<> // TIMER1
{
	static constexpr auto *TCNTn  = reinterpret_cast<volatile uint16_t * const>(&TCNT1);
	static constexpr auto *TCNTnL = reinterpret_cast<volatile uint8_t *>(&TCNT1L);
	static constexpr auto *TCNTnH = reinterpret_cast<volatile uint8_t *>(&TCNT1H);
	static constexpr auto *TCCRnA = reinterpret_cast<volatile uint8_t *>(&TCCR1A);
	static constexpr auto *TCCRnB = reinterpret_cast<volatile uint8_t *>(&TCCR1B);
	static constexpr auto *TCCRnC = reinterpret_cast<volatile uint8_t *>(&TCCR1C);
	static constexpr auto *OCRnA  = reinterpret_cast<volatile uint16_t *>(&OCR1A);
	static constexpr auto *OCRnAL = reinterpret_cast<volatile uint8_t *>(&OCR1AL);
	static constexpr auto *OCRnAH = reinterpret_cast<volatile uint8_t *>(&OCR1AH);
	static constexpr auto *OCRnB  = reinterpret_cast<volatile uint16_t *>(&OCR1B);
	static constexpr auto *OCRnBL = reinterpret_cast<volatile uint8_t *>(&OCR1BL);
	static constexpr auto *OCRnBH = reinterpret_cast<volatile uint8_t *>(&OCR1BH);
	static constexpr auto *ICRn   = reinterpret_cast<volatile uint16_t *>(&ICR1);
	static constexpr auto *ICRnL  = reinterpret_cast<volatile uint8_t *>(&ICR1L);
	static constexpr auto *ICRnH  = reinterpret_cast<volatile uint8_t *>(&ICR1H);
	static constexpr auto *TIMSKn = reinterpret_cast<volatile uint8_t *>(&TIMSK1);
	static constexpr auto *TIFRn  = reinterpret_cast<volatile uint8_t *>(&TIFR1);

	static constexpr auto *PORTn = reinterpret_cast<volatile uint8_t *>(&PORTB);
	static constexpr auto *PINn  = reinterpret_cast<volatile uint8_t *>(&PINB);
	static constexpr auto *DDRn  = reinterpret_cast<volatile uint8_t *>(&DDRB);
	
	static constexpr auto OCnA = PB5;
	static constexpr auto OCnB = PB6;
	static constexpr auto OCnC = PB7;
};
#endif

template<> struct OneShortPulseTraits<3> : OneShortPulseTraits<> // TIMER3
{
	static constexpr auto *TCNTn  = reinterpret_cast<volatile uint16_t *>(&TCNT3);
	static constexpr auto *TCNTnL = reinterpret_cast<volatile uint8_t *>(&TCNT3L);
	static constexpr auto *TCNTnH = reinterpret_cast<volatile uint8_t *>(&TCNT3H);
	static constexpr auto *TCCRnA = reinterpret_cast<volatile uint8_t *>(&TCCR3A);
	static constexpr auto *TCCRnB = reinterpret_cast<volatile uint8_t *>(&TCCR3B);
	static constexpr auto *TCCRnC = reinterpret_cast<volatile uint8_t *>(&TCCR3C);
	static constexpr auto *OCRnA  = reinterpret_cast<volatile uint16_t *>(&OCR3A);
	static constexpr auto *OCRnAL = reinterpret_cast<volatile uint8_t *>(&OCR3AL);
	static constexpr auto *OCRnAH = reinterpret_cast<volatile uint8_t *>(&OCR3AH);
	static constexpr auto *OCRnB  = reinterpret_cast<volatile uint16_t *>(&OCR3B);
	static constexpr auto *OCRnBL = reinterpret_cast<volatile uint8_t *>(&OCR3BL);
	static constexpr auto *OCRnBH = reinterpret_cast<volatile uint8_t *>(&OCR3BH);
	static constexpr auto *ICRn   = reinterpret_cast<volatile uint16_t *>(&ICR3);
	static constexpr auto *ICRnL  = reinterpret_cast<volatile uint8_t *>(&ICR3L);
	static constexpr auto *ICRnH  = reinterpret_cast<volatile uint8_t *>(&ICR3H);
	static constexpr auto *TIMSKn = reinterpret_cast<volatile uint8_t *>(&TIMSK3);
	static constexpr auto *TIFRn  = reinterpret_cast<volatile uint8_t *>(&TIFR3);

	static constexpr auto *PORTn = reinterpret_cast<volatile uint8_t *>(&PORTE);
	static constexpr auto *PINn  = reinterpret_cast<volatile uint8_t *>(&PINE);
	static constexpr auto *DDRn  = reinterpret_cast<volatile uint8_t *>(&DDRE);

	static constexpr auto OCnA = PE3;
	static constexpr auto OCnB = PE4;
	static constexpr auto OCnC = PE5;
};

#if 0
template<> struct OneShortPulseTraits<4> : OneShortPulseTraits<> // TIMER4
{
	static constexpr auto *TCNTn  = reinterpret_cast<volatile uint16_t *>(&TCNT4);
	static constexpr auto *TCNTnL = reinterpret_cast<volatile uint8_t *>(&TCNT4L);
	static constexpr auto *TCNTnH = reinterpret_cast<volatile uint8_t *>(&TCNT4H);
	static constexpr auto *TCCRnA = reinterpret_cast<volatile uint8_t *>(&TCCR4A);
	static constexpr auto *TCCRnB = reinterpret_cast<volatile uint8_t *>(&TCCR4B);
	static constexpr auto *TCCRnC = reinterpret_cast<volatile uint8_t *>(&TCCR4C);
	static constexpr auto *OCRnA  = reinterpret_cast<volatile uint16_t *>(&OCR4A);
	static constexpr auto *OCRnAL = reinterpret_cast<volatile uint8_t *>(&OCR4AL);
	static constexpr auto *OCRnAH = reinterpret_cast<volatile uint8_t *>(&OCR4AH);
	static constexpr auto *OCRnB  = reinterpret_cast<volatile uint16_t *>(&OCR4B);
	static constexpr auto *OCRnBL = reinterpret_cast<volatile uint8_t *>(&OCR4BL);
	static constexpr auto *OCRnBH = reinterpret_cast<volatile uint8_t *>(&OCR4BH);
	static constexpr auto *ICRn   = reinterpret_cast<volatile uint16_t *>(&ICR4);
	static constexpr auto *ICRnL  = reinterpret_cast<volatile uint8_t *>(&ICR4L);
	static constexpr auto *ICRnH  = reinterpret_cast<volatile uint8_t *>(&ICR4H);
	static constexpr auto *TIMSKn = reinterpret_cast<volatile uint8_t *>(&TIMSK4);
	static constexpr auto *TIFRn  = reinterpret_cast<volatile uint8_t *>(&TIFR4);

	static constexpr auto *PORTn = reinterpret_cast<volatile uint8_t *>(&PORTH);
	static constexpr auto *PINn  = reinterpret_cast<volatile uint8_t *>(&PINH);
	static constexpr auto *DDRn  = reinterpret_cast<volatile uint8_t *>(&DDRH);

	static constexpr auto OCnA = PH3;
	static constexpr auto OCnB = PH4;
	static constexpr auto OCnC = PH5;
};

template<> struct OneShortPulseTraits<5> : OneShortPulseTraits<> // TIMER5
{
	static constexpr auto *TCNTn  = reinterpret_cast<volatile uint16_t *>(&TCNT5);
	static constexpr auto *TCNTnL = reinterpret_cast<volatile uint8_t *>(&TCNT5L);
	static constexpr auto *TCNTnH = reinterpret_cast<volatile uint8_t *>(&TCNT5H);
	static constexpr auto *TCCRnA = reinterpret_cast<volatile uint8_t *>(&TCCR5A);
	static constexpr auto *TCCRnB = reinterpret_cast<volatile uint8_t *>(&TCCR5B);
	static constexpr auto *TCCRnC = reinterpret_cast<volatile uint8_t *>(&TCCR5C);
	static constexpr auto *OCRnA  = reinterpret_cast<volatile uint16_t *>(&OCR5A);
	static constexpr auto *OCRnAL = reinterpret_cast<volatile uint8_t *>(&OCR5AL);
	static constexpr auto *OCRnAH = reinterpret_cast<volatile uint8_t *>(&OCR5AH);
	static constexpr auto *OCRnB  = reinterpret_cast<volatile uint16_t *>(&OCR5B);
	static constexpr auto *OCRnBL = reinterpret_cast<volatile uint8_t *>(&OCR5BL);
	static constexpr auto *OCRnBH = reinterpret_cast<volatile uint8_t *>(&OCR5BH);
	static constexpr auto *ICRn   = reinterpret_cast<volatile uint16_t *>(&ICR5);
	static constexpr auto *ICRnL  = reinterpret_cast<volatile uint8_t *>(&ICR5L);
	static constexpr auto *ICRnH  = reinterpret_cast<volatile uint8_t *>(&ICR5H);
	static constexpr auto *TIMSKn = reinterpret_cast<volatile uint8_t *>(&TIMSK5);
	static constexpr auto *TIFRn  = reinterpret_cast<volatile uint8_t *>(&TIFR5);

	static constexpr auto *PORTn = reinterpret_cast<volatile uint8_t *>(&PORTL);
	static constexpr auto *PINn  = reinterpret_cast<volatile uint8_t *>(&PINL);
	static constexpr auto *DDRn  = reinterpret_cast<volatile uint8_t *>(&DDRL);

	static constexpr auto OCnA = PL3;
	static constexpr auto OCnB = PL4;
	static constexpr auto OCnC = PL5;
};
#endif

template<byte TIMER>
class OneShortPulse : OneShortPulseTraits<TIMER>
{
	unsigned long	time_point;

public:
	using OneShortPulseTraits<TIMER>::COMnA1;
	using OneShortPulseTraits<TIMER>::COMnA0;
	using OneShortPulseTraits<TIMER>::COMnB1;
	using OneShortPulseTraits<TIMER>::COMnB0;
	using OneShortPulseTraits<TIMER>::COMnC1;
	using OneShortPulseTraits<TIMER>::COMnC0;
	using OneShortPulseTraits<TIMER>::WGMn1;
	using OneShortPulseTraits<TIMER>::WGMn0;
	using OneShortPulseTraits<TIMER>::ICNCn;
	using OneShortPulseTraits<TIMER>::ICESn;
	using OneShortPulseTraits<TIMER>::WGMn3;
	using OneShortPulseTraits<TIMER>::WGMn2;
	using OneShortPulseTraits<TIMER>::CSn2;
	using OneShortPulseTraits<TIMER>::CSn1;
	using OneShortPulseTraits<TIMER>::CSn0;
	using OneShortPulseTraits<TIMER>::FOCnA;
	using OneShortPulseTraits<TIMER>::FOCnB;
	using OneShortPulseTraits<TIMER>::FOCnC;

	using OneShortPulseTraits<TIMER>::TCNTn;
	using OneShortPulseTraits<TIMER>::TCNTnL;
	using OneShortPulseTraits<TIMER>::TCNTnH;
	using OneShortPulseTraits<TIMER>::TCCRnA;
	using OneShortPulseTraits<TIMER>::TCCRnB;
	using OneShortPulseTraits<TIMER>::TCCRnC;
	using OneShortPulseTraits<TIMER>::OCRnA;
	using OneShortPulseTraits<TIMER>::OCRnAL;
	using OneShortPulseTraits<TIMER>::OCRnAH;
	using OneShortPulseTraits<TIMER>::OCRnB;
	using OneShortPulseTraits<TIMER>::OCRnBL;
	using OneShortPulseTraits<TIMER>::OCRnBH;
	using OneShortPulseTraits<TIMER>::ICRn;
	using OneShortPulseTraits<TIMER>::ICRnL;
	using OneShortPulseTraits<TIMER>::ICRnH;
	using OneShortPulseTraits<TIMER>::TIMSKn;
	using OneShortPulseTraits<TIMER>::TIFRn;

	using OneShortPulseTraits<TIMER>::PORTn;
	using OneShortPulseTraits<TIMER>::PINn;
	using OneShortPulseTraits<TIMER>::DDRn;

	using OneShortPulseTraits<TIMER>::OCnA;
	using OneShortPulseTraits<TIMER>::OCnB;
	using OneShortPulseTraits<TIMER>::OCnC;

	OneShortPulse() : time_point(micros())
	{}

	inline void setup(bool b)
	{
		*TCCRnB = 0;
		*TCNTn = 0x0000;
		*ICRn = 0;
		*OCRnB = 0xffff;
		*TCCRnA = (b << COMnB0) | (1 << COMnB1) | (1 << WGMn1);
		*TCCRnB = (1 << WGMn2) | (1 << WGMn3) | (1 << CSn0);
		*DDRn = (1 << OCnB);
	}

	inline bool inProgress()
	{
		return *TCNTn > 0;
	}

	static inline void fire()
	{
		*TCNTn = *OCRnB - 1;
	}

	static inline void fire(uint16_t cycles)
	{
		uint16_t m = 0xffff - (cycles - 1);
		*OCRnB = m;
		*TCNTn = m - 1;
	}

	inline void fire(unsigned long mark, unsigned long total)
	{
		time_point = micros() + total;
		fire(uint16_t(mark * (F_CPU / 1000000)));
	}

	inline void adjustWait(unsigned long us)
	{
		time_point += us;
	}


	inline void wait()
	{
		while (micros() < time_point);
	}

	inline void setLevel(bool b)
	{
		if (b)
		{
			*TCCRnA |= (1 << COMnB0);
		}
		else
		{
			*TCCRnA &= ~(1 << COMnB0);
		}
	}

	inline void start()
	{
		*TCNTn = 0x0000;
		*OCRnB = 0xffff;
		*TCCRnB = (1 << WGMn2) | (1 << WGMn3) | (1 << CSn0);
		time_point = micros();
	}

	inline void stop()
	{
		*TCCRnB = 0;
		time_point = micros();
	}
};
