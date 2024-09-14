#pragma once

struct OneShortPulseTraits
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

	static constexpr auto OCnA = PE3;
	static constexpr auto OCnB = PE4;
	static constexpr auto OCnC = PE5;
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

template<> struct OneShortPulseTraits<3> : OneShortPulseTraits<> // TIMER3
{
protected:
	static constexpr auto A0 = 0x90;
	static constexpr auto A1 = 0x71;
	static constexpr auto A2 = 0x18;

public:
	static const auto TCCRnA = reinterpret_cast<volatile uint8_t  *>(0x90);
	static constexpr auto TCCRnB = reinterpret_cast<volatile uint8_t  *>(A0+0x1);
	static constexpr auto TCCRnC = reinterpret_cast<volatile uint8_t  *>(A0+0x2);
	static constexpr auto TCNTn  = reinterpret_cast<volatile uint16_t *>(A0+0x4);
	static constexpr auto TCNTnL = reinterpret_cast<volatile uint8_t  *>(A0+0x4);
	static constexpr auto TCNTnH = reinterpret_cast<volatile uint8_t  *>(A0+0x5);
	static constexpr auto ICRn   = reinterpret_cast<volatile uint16_t *>(A0+0x6);
	static constexpr auto ICRnL  = reinterpret_cast<volatile uint8_t  *>(A0+0x6);
	static constexpr auto ICRnH  = reinterpret_cast<volatile uint8_t  *>(A0+0x7);
	static constexpr auto OCRnA  = reinterpret_cast<volatile uint16_t *>(A0+0x8);
	static constexpr auto OCRnAL = reinterpret_cast<volatile uint8_t  *>(A0+0x8);
	static constexpr auto OCRnAH = reinterpret_cast<volatile uint8_t  *>(A0+0x9);
	static constexpr auto OCRnB  = reinterpret_cast<volatile uint16_t *>(A0+0xA);
	static constexpr auto OCRnBL = reinterpret_cast<volatile uint8_t  *>(A0+0xA);
	static constexpr auto OCRnBH = reinterpret_cast<volatile uint8_t  *>(A0+0xB);
	static constexpr auto TIMSKn = reinterpret_cast<volatile uint8_t  *>(A1);
	static constexpr auto TIFRn  = reinterpret_cast<volatile uint8_t  *>(A2);

	static constexpr auto PINn  = reinterpret_cast<volatile uint8_t *>(0x0C);
	static constexpr auto DDRn  = reinterpret_cast<volatile uint8_t *>(0x0D);
	static constexpr auto PORTn = reinterpret_cast<volatile uint8_t *>(0x0E);

	static constexpr auto OCnA = PE3;
	static constexpr auto OCnB = PE4;
	static constexpr auto OCnC = PE5;
};

µtemplate<> struct OneShortPulseTraits<4> : OneShortPulseTraits<> // TIMER4
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

class OneShortPulse : OneShortPulseTraits
{
	unsigned long	time_point;

public:
	using OneShortPulseTraits::COMnA1;
	using OneShortPulseTraits::COMnA0;
	using OneShortPulseTraits::COMnB1;
	using OneShortPulseTraits::COMnB0;
	using OneShortPulseTraits::COMnC1;
	using OneShortPulseTraits::COMnC0;
	using OneShortPulseTraits::WGMn1;
	using OneShortPulseTraits::WGMn0;
	using OneShortPulseTraits::ICNCn;
	using OneShortPulseTraits::ICESn;
	using OneShortPulseTraits::WGMn3;
	using OneShortPulseTraits::WGMn2;
	using OneShortPulseTraits::CSn2;
	using OneShortPulseTraits::CSn1;
	using OneShortPulseTraits::CSn0;
	using OneShortPulseTraits::FOCnA;
	using OneShortPulseTraits::FOCnB;
	using OneShortPulseTraits::FOCnC;

	using OneShortPulseTraits::OCnA;
	using OneShortPulseTraits::OCnB;
	using OneShortPulseTraits::OCnC;

	OneShortPulse() : time_point(micros())
	{}

	inline void setup(bool b)
	{
		TCCR3B = 0;
		TCNT3 = 0x0000;
		ICR3 = 0;
		OCR3B = 0xffff;
		TCCR3A = (b << COMnB0) | (1 << COMnB1) | (1 << WGMn1);
		TCCR3B = (1 << WGMn2) | (1 << WGMn3) | (1 << CSn0);
		DDRE = (1 << OCnB);
	}

	inline bool inProgress()
	{
		return TCNT3 > 0;
	}

	static inline void fire()
	{
		TCNT3 = OCR3B - 1;
	}

	static inline void fire(uint16_t cycles)
	{
		uint16_t m = 0xffff - (cycles - 1);
		OCR3B = m;
		TCNT3 = m - 1;
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
			TCCR3A |= (1 << COMnB0);
		}
		else
		{
			TCCR3A &= ~(1 << COMnB0);
		}
	}

	inline void start()
	{
		TCNT3 = 0x0000;
		OCR3B = 0xffff;
		TCCR3B = (1 << WGMn2) | (1 << WGMn3) | (1 << CSn0);
		time_point = micros();
	}

	inline void stop()
	{
		TCCR3B = 0;
		time_point = micros();
	}
};
