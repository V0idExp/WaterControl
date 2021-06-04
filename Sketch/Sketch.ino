#include <EncButton.h>
#include <FastIO.h>
#include <LiquidCrystal.h>


// Time constants in seconds
#define SECOND 1UL
#define MINUTE (60UL * SECOND)
#define HOUR (MINUTE * 60UL)
#define DAY (HOUR * 24UL)


// Custom characters, used as widget icons
#define CLOCK_CHR byte(1)
#define DROP_CHR byte(2)
#define WATCH_CHR byte(3)
#define VALVE_CHR byte(4)
#define WATER_CHR byte(5)
#define STOP_CHR byte(6)

uint8_t clock_char[] = {
	0x0, 0x0, 0xe, 0x15, 0x17, 0x11, 0xe, 0x0
};

uint8_t drop_char[] = {
	0x0, 0x0, 0x4, 0xe, 0x1f, 0x1f, 0xe, 0x0
};

uint8_t stopwatch_char[] = {
	0xe, 0x4, 0xe, 0x11, 0x15, 0x19, 0xe, 0x0
};

uint8_t valve_char[] = {
	0x0, 0x0, 0xe, 0x4, 0x1f, 0x1f, 0x18, 0x0
};

uint8_t water_char[] = {
	0x7, 0x4, 0xe, 0x1f, 0x0, 0x15, 0x15, 0x0
};

uint8_t water_stop_char[] = {
	0x7, 0x4, 0xe, 0x1f, 0x0, 0x0, 0x0, 0x0
};


// Feed control vars
unsigned long feed_period = 1 * MINUTE + SECOND;
unsigned long feed_countdown = feed_period;
unsigned long feed_intensity = 75;
unsigned long feed_duration = 50;
bool feed_active = false;


// Time vars
unsigned long last_update = 0, now, dt, elapsed_seconds, total_seconds = 0;


// Thresholds for time field widgets
static unsigned long thresholds[] = { DAY, HOUR, MINUTE, SECOND };


// LCD interface
const int rs = 7, en = 6, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


// Encoder interface
EncButton<EB_TICK, 12, 11, 13> enc;


// PWM pin
const int pwm = 10;


// Button labels
static const char *label_feed = "Feed";
static const char *label_stop = "Stop";

struct Widget
{
	enum class Type
	{
		FIELD,
		BUTTON
	} type;

	unsigned col;
	unsigned row;

	byte icon;

	union
	{
		struct Field
		{
			unsigned long *value;
			unsigned long min;
			unsigned long max;
			void (*formatter)(char *dst, unsigned long value);
			void (*setter)(Widget &w, int increment);
		} field;

		struct Button
		{
			const char *label;
			void (*handler)(Widget &w);
		} button;
	};
};


struct UI
{
	enum
	{
		SELECT,
		EDIT
	} state = SELECT;
	unsigned selected = 0;
} ui;


void format_duration(char *dst, unsigned long value);
void format_percentile(char *dst, unsigned long value);
void set_duration(Widget &w, int increment);
void set_pwm(Widget &w, int increment);
void toggle_feed(Widget &w);


static Widget widgets[] = {
	// Feed period
	{
		Widget::Type::FIELD,
		0,
		0,
		CLOCK_CHR,
		{.field={&feed_period, 1, 31 * DAY, format_duration, set_duration}},
	},
	// Feed duration
	{
		Widget::Type::FIELD,
		5,
		0,
		DROP_CHR,
		{.field={&feed_duration, 1, 60, format_duration, set_duration}},
	},
	// Feed toggle button
	{
		Widget::Type::BUTTON,
		10,
		0,
		WATER_CHR,
		{.button = {label_feed, toggle_feed}},
	},
	// Feed countdown
	{
		Widget::Type::FIELD,
		0,
		1,
		WATCH_CHR,
		{.field={&feed_countdown, 1, 99, format_duration, set_duration}},
	},
	// Feed intensity
	{
		Widget::Type::FIELD,
		5,
		1,
		VALVE_CHR,
		{.field={&feed_intensity, 0, 99, format_percentile, set_pwm}},
	},
};

#define WIDGETS_COUNT (sizeof(widgets) / sizeof(Widget))


void setup()
{
	// Serial.begin(9600);

	// set up the LCD's number of columns and rows:
	lcd.begin(16, 2);
	lcd.display();
	lcd.createChar(CLOCK_CHR, clock_char);
	lcd.createChar(DROP_CHR, drop_char);
	lcd.createChar(WATCH_CHR, stopwatch_char);
	lcd.createChar(VALVE_CHR, valve_char);
	lcd.createChar(WATER_CHR, water_char);
	lcd.createChar(STOP_CHR, water_stop_char);

	last_update = millis();

	// set up a 15.6 kHz PWM signal
	pinMode(pwm, OUTPUT);
	TCCR1A = 0b00000011;  // 10bit
	TCCR1B = 0b00001001;  // x1 fast pwm

	update_ui();
}

void update_ui()
{
	lcd.clear();

	for (int i = 0; i < WIDGETS_COUNT; i++)
	{
		Widget &w = widgets[i];

		lcd.setCursor(w.col, w.row);

		if (ui.state == UI::SELECT)
		{
			lcd.print(i == ui.selected ? '>' : ' ');
		}
		else if (ui.state == UI::EDIT)
		{
			lcd.print(i == ui.selected ? '=' : ' ');
		}
		else
		{
			lcd.print(' ');
		}

		lcd.write(w.icon);

		if (w.type == Widget::Type::FIELD)
		{
			char buf[4] = {0};
			w.field.formatter(buf, *(w.field.value));
			lcd.print(buf);
		}
		else if (w.type == Widget::Type::BUTTON)
		{
			lcd.print(w.button.label);
		}
	}
}

void tick_time() {
	// compute time delta since last tick in milliseconds, accounting for overflow
	now = millis();
	if (now >= last_update) {
		dt += now - last_update;
	}
	else {
		// the timer overflowed
		dt += (-1UL - last_update) + now;
	}
	last_update = now;
	elapsed_seconds = dt / 1000;
	dt -= elapsed_seconds * 1000;
	total_seconds += elapsed_seconds;
}

void loop()
{
	enc.tick();
	tick_time();

	bool should_update = false;

	if (elapsed_seconds > 0)
	{
		if (feed_countdown >= elapsed_seconds)
		{
			feed_countdown -= elapsed_seconds;
			should_update = true;
		}
		else
		{
			feed_countdown = feed_period;
			should_update = true;
		}
	}

	if (ui.state == UI::SELECT)
	{
		if (enc.isLeft())
		{
			if (ui.selected == 0)
			{
				ui.selected = WIDGETS_COUNT - 1;
			}
			else
			{
				ui.selected--;
			}
		}
		else if (enc.isRight())
		{
			if (ui.selected == WIDGETS_COUNT - 1)
			{
				ui.selected = 0;
			}
			else
			{
				ui.selected++;
			}
		}

		if (enc.isClick())
		{
			Widget &w = widgets[ui.selected];
			should_update = true;
			switch (w.type)
			{
				case Widget::Type::FIELD:
					ui.state = UI::EDIT;
					enc.counter = 0;
					break;
				case Widget::Type::BUTTON:
					w.button.handler(w);
			}
		}

		if (enc.isTurn())
		{
			should_update = true;
		}
	}

	if (ui.state == UI::EDIT)
	{
		if (enc.isTurn())
		{
			Widget &w = widgets[ui.selected];
			int increment = (enc.isLeft() ? -1 : 1) * (enc.isFast() ? 5 : 1);

			unsigned long value = *(w.field.value);
			w.field.setter(w, increment);

			if (value != *(w.field.value))
			{
				should_update = true;
			}
		}

		if (enc.isClick())
		{
			ui.state = UI::SELECT;
			should_update = true;
		}
	}

	if (should_update)
	{
		update_ui();
	}
}


void format_duration(char *dst, unsigned long value)
{
	static char units[] = {'d', 'h', 'm', 's'};
	static unsigned long thresholds[] = { DAY, HOUR, MINUTE, SECOND };

	for (int i = 0; i < 4; i++)
	{
		if (value >= thresholds[i])
		{
			value = value / thresholds[i];
			snprintf(dst, 4, "%02lu%c", value, units[i]);
			break;
		}
	}
}

void set_duration(Widget &w, int increment)
{
	unsigned long value = *w.field.value;
	int i;
	for (i = 0; i < 4; i++)
	{
		if (value >= thresholds[i])
		{
			break;
		}
	}

	unsigned long threshold = thresholds[i];
	unsigned long new_val = value + increment * threshold;

	// trim the the value to a multiple of threshold
	value -= value % threshold;

	if (increment < 0)
	{
		if (new_val < threshold || new_val > value)
		{
			// the decrement is bigger than the value or an underflow occurred,
			// subtract the next lower threshold
			value = threshold - (i < 3 ? thresholds[i + 1] : 1);
		}
		else
		{
			value = new_val;
		}
	}
	else
	{
		if (new_val < value)
		{
			// overflow; make the value a maximum multiple of threshold that can
			// be fit into an unsigned long
			value = -1UL - (-1UL % threshold);
		}
		else if (i > 0 && new_val >= thresholds[i - 1])
		{
			// next threshold reached
			value = thresholds[i - 1];
		}
		else
		{
			value = new_val;
		}
	}

	*(w.field.value) = constrain(value, w.field.min, w.field.max);
}

void format_percentile(char *dst, unsigned long value)
{
	snprintf(dst, 4, "%02lu%%", value);
}

void set_pwm(Widget &w, int increment)
{
	unsigned long v = *(w.field.value);
	if (increment < 0 && v + increment > v)
	{
		// underflow
		v = 0;
	}
	else if (increment > 0 && v + increment < v)
	{
		// overflow
		v = -1UL;
	}
	else
	{
		v += increment;
	}

	*(w.field.value) = constrain(v, w.field.min, w.field.max);

	update_pwm();
}

void update_pwm()
{
	analogWrite(pwm, feed_active * (feed_intensity / 100.0) * 1024);
}

void toggle_feed(Widget &w)
{
	feed_active = !feed_active;

	w.icon = feed_active ? STOP_CHR : WATER_CHR;
	w.button.label = feed_active ? label_stop : label_feed;

	update_pwm();
}
