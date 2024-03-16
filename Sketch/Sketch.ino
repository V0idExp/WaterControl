#include <LCD_I2C.h>
#include <EEPROM.h>
// #include <EncButton.h>

// Time constants in seconds
#define SECOND 1UL
#define MINUTE (SECOND * 60UL)
#define HOUR (MINUTE * 60UL)
#define DAY (HOUR * 24UL)

// Backlight timeout
#define BACKLIGHT_TIMEOUT (SECOND * 30)

// Custom characters, used as widget icons
#define CLOCK_CHR byte(1)
#define DROP_CHR byte(2)
#define WATCH_CHR byte(3)
#define VALVE_CHR byte(4)
#define WATER_CHR byte(5)
#define STOP_CHR byte(6)
#define FILL_CHR byte(7)


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

uint8_t water_tank_char[] = {
	0xe, 0x11, 0x1f, 0x1f, 0x1f, 0x1f, 0xe, 0x0
};

typedef unsigned long seconds;  // seconds

/**
 * Water feed settings (pump on/off cycle and power).
*/
struct FeedSettings
{
	seconds interval;
	seconds duration;
	seconds start;
	seconds end;
} feed_settings[2] = {
	// Day
	{
		interval: HOUR * 2,
		duration: MINUTE * 1,
		start: HOUR * 9,
		end: HOUR * 18,
	},
	// Night
	{
		interval: HOUR * 4,
		duration: MINUTE * 1,
		start: HOUR * 18,
		end: HOUR * 9,
	},
};

const FeedSettings *active_phase = &feed_settings[0];
seconds feed_countdown = 0;
byte feed_intensity = 100;
bool feed_active = false;
bool tank_empty = false;

// EEPROM indices for saving vars
struct EEStore
{
	int magic;
	unsigned long feed_period;
	unsigned long feed_intensity;
	unsigned long feed_duration;
};

// Magic byte to ensure existing settings are valid
#define EEMAGIC 0xbe1

// Time vars
unsigned long last_update = 0, now, dt, elapsed_seconds, total_seconds = 0;
long backlight_remaining_seconds = BACKLIGHT_TIMEOUT;

// Thresholds for seconds field widgets
static unsigned long thresholds[] = { DAY, HOUR, MINUTE, SECOND };

// LCD interface (Address, cols, rows)
LCD_I2C lcd(0x27, 16, 2);

// Encoder interface (A, B, Button)
// EncButton<EB_TICK, 12, 11, 13> enc;

// PWM pin
const int pwm = 10;

// Water sensor pin
const int sensor = 8;

// Button labels
static const char *label_feed = "Feed";
static const char *label_stop = "Stop";
static const char *label_fill = "Fill";

struct Pos
{
	unsigned col;
	unsigned row;
};

struct Widget
{
	enum class Type
	{
		EMPTY,
		FIELD,
		BUTTON,
		LABEL,
	} type;

	Pos pos;

	struct Field
	{
		void *value;
		unsigned long min;
		unsigned long max;
	} field;

	void (*on_update)(Widget &w);
	void (*on_set)(Widget &w, int increment);
	void (*on_click)(Widget &w);

	byte icon;
	char *label;
};

struct UI
{
	enum
	{
		SELECT,
		EDIT
	} state = SELECT;

	unsigned selected = 0;
	unsigned page = 0;
} ui;

void update_duration(Widget &w);
void update_percentile(Widget &w);
void update_feed_button(Widget &w);
void update_time(Widget &w);
void set_duration(Widget &w, int increment);
void set_pwm(Widget &w, int increment);
void set_time(Widget &w, int increment);
void toggle_feed(Widget &w);
void update_feed(unsigned dt);

static const Pos TOP_LEFT = {0, 0};
static const Pos TOP_MIDDLE = { 5, 0 };
static const Pos TOP_RIGHT = { 10, 0 };
static const Pos BOTTOM_LEFT = { 0, 1 };
static const Pos BOTTOM_MIDDLE = { 5, 1 };
static const Pos BOTTOM_RIGHT = { 10, 1 };

static Widget widgets[] = {
	/*** FEED SETTINGS ***/
	// "Day" label
	{
		type: Widget::Type::LABEL,
		pos: TOP_LEFT,
		field: {},
		on_update: nullptr,
		on_set: nullptr,
		on_click: nullptr,
		icon: 0,
		label: "Day",
	},
	// Day start time
	{
		type: Widget::Type::FIELD,
		pos: TOP_MIDDLE,
		field: {
			value: &feed_settings[0].start,
			min: 0UL,
			max: HOUR * 24,
		},
		on_update: update_time,
		on_set: set_time,
		on_click: nullptr,
		icon: 0,
		label: nullptr,
	},
	// Day end time
	{
		type: Widget::Type::FIELD,
		pos: TOP_RIGHT,
		field: {
			value: &feed_settings[0].end,
			min: 0UL,
			max: HOUR * 24,
		},
		on_update: update_time,
		on_set: set_time,
		on_click: nullptr,
		icon: 0,
		label: nullptr,
	},
	// Feed periodicity (interval between feeds)
	{
		type: Widget::Type::FIELD,
		pos: BOTTOM_LEFT,
		field: {
			value: &feed_settings[0].interval,
			min: 1UL,
			max: 31 * DAY,
		},
		on_update: update_duration,
		on_set: set_duration,
		on_click: nullptr,
		icon: CLOCK_CHR,
		label: nullptr,
	},
	// Feed duration
	{
		type: Widget::Type::FIELD,
		pos: BOTTOM_MIDDLE,
		field: {
			value: &feed_settings[0].duration,
			min: 1UL,
			max: HOUR,
		},
		on_update: update_duration,
		on_set: set_duration,
		on_click: nullptr,
		icon: DROP_CHR,
		label: nullptr,
	},
	// Empty space
	{
		type: Widget::Type::EMPTY,
		pos: BOTTOM_RIGHT,
	},
	// // Feed toggle button
	// {
	// 	type: Widget::Type::BUTTON,
	// 	pos: TOP_RIGHT,
	// 	field: {},
	// 	on_update: update_feed_button,
	// 	on_set: nullptr,
	// 	on_click: toggle_feed,
	// 	icon: WATER_CHR,
	// 	label: nullptr,
	// },
	// // Feed countdown
	// {
	// 	type: Widget::Type::FIELD,
	// 	pos: BOTTOM_LEFT,
	// 	field: {
	// 		value: &feed_settings[0],
	// 		min: 1UL,
	// 		max: 31 * DAY
	// 	},
	// 	on_update: update_duration,
	// 	on_set: set_duration,
	// 	on_click: nullptr,
	// 	icon: WATCH_CHR,
	// 	label: nullptr,
	// },
	// // Feed intensity
	// {
	// 	type: Widget::Type::FIELD,
	// 	pos: BOTTOM_MIDDLE,
	// 	field: {
	// 		value: &day_feed.intensity,
	// 		min: 0UL,
	// 		max: 99UL,
	// 	},
	// 	on_update: update_percentile,
	// 	on_set: set_pwm,
	// 	on_click: nullptr,
	// 	icon: VALVE_CHR,
	// 	label: nullptr,
	// },

	// /*** NIGHT FEED SETTINGS ***/
	// // Feed periodicity (interval between feeds)
	// {
	// 	Widget::Type::FIELD, TOP_LEFT,
	// 	.field={&day_feed.interval, 1, 31 * DAY},
	// 	.on_update = update_duration,
	// 	.on_set = set_duration,
	// 	.icon = CLOCK_CHR,
	// },
	// // Feed duration
	// {
	// 	Widget::Type::FIELD, TOP_MIDDLE,
	// 	.field={&day_feed.duration, 1, HOUR},
	// 	.on_update = update_duration,
	// 	.on_set = set_duration,
	// 	.icon = DROP_CHR,
	// },
	// // Feed toggle button
	// {
	// 	Widget::Type::BUTTON, TOP_RIGHT,
	// 	.on_update = update_feed_button,
	// 	.on_click = toggle_feed,
	// 	.icon = WATER_CHR,
	// },
	// // Feed countdown
	// {
	// 	Widget::Type::FIELD, BOTTOM_LEFT,
	// 	.field={&feed_countdown, 1, 31 * DAY},
	// 	.on_update = update_duration,
	// 	.on_set = set_duration,
	// 	.icon = WATCH_CHR,
	// },
	// // Feed intensity
	// {
	// 	Widget::Type::FIELD, BOTTOM_MIDDLE,
	// 	.field={&day_feed.intensity, 0, 99},
	// 	.on_update = update_percentile,
	// 	.on_set = set_pwm,
	// 	.icon = VALVE_CHR,
	// },
	// // "Day" label
	// {
	// 	Widget::Type::LABEL, BOTTOM_RIGHT,
	// 	.label = "Day",
	// },
};

static constexpr const int PAGE_SIZE = 16;
static constexpr const int WIDGETS_COUNT = (sizeof(widgets) / sizeof(Widget));
static constexpr const int PAGES = WIDGETS_COUNT / PAGE_SIZE;
static_assert(WIDGETS_COUNT % 6 == 0);

void setup()
{
	Serial.begin(9600);

	// load the last feed settings from the EEPROM
	load_values_from_eeprom();

	// set up the LCD's number of columns and rows:
	lcd.begin();
	lcd.backlight();

	lcd.createChar(CLOCK_CHR, clock_char);
	lcd.createChar(DROP_CHR, drop_char);
	lcd.createChar(WATCH_CHR, stopwatch_char);
	lcd.createChar(VALVE_CHR, valve_char);
	lcd.createChar(WATER_CHR, water_char);
	lcd.createChar(STOP_CHR, water_stop_char);
	lcd.createChar(FILL_CHR, water_tank_char);
	last_update = millis();

	// set up a 15.6 kHz PWM signal
	pinMode(pwm, OUTPUT);
	TCCR1A = 0b00000011; // 10bit
	TCCR1B = 0b00001001; // x1 fast pwm

	pinMode(sensor, INPUT);

	update_ui();
}


void update_ui()
{
	lcd.clear();

	// render the widgets of the current page
	for (int offset = ui.page * PAGE_SIZE, i = offset; i < offset + PAGE_SIZE; i++)
	{
		Widget &w = widgets[i];

		// update the widget
		if (w.on_update)
		{
			w.on_update(w);
		}

		// position the cursor on the beginning of the widget region
		lcd.setCursor(w.pos.col, w.pos.row);

		// render the cursor
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

		// render the icon
		if (w.icon)
		{
			lcd.write(w.icon);
		}
		else
		{
			lcd.write(' ');
		}

		// render the label string
		if (w.label)
		{
			lcd.print(w.label);
		}
	}

	// tick the backlight
	backlight_remaining_seconds -= elapsed_seconds;
	if (backlight_remaining_seconds < 0)
	{
		lcd.noBacklight();
		backlight_remaining_seconds = 0;
	}
	else
	{
		lcd.backlight();
	}
}


void tick_time()
{
	// compute time delta since last tick in milliseconds, accounting for overflow
	now = millis();
	if (now >= last_update)
	{
		dt += now - last_update;
	}
	else
	{
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
	// enc.tick();
	tick_time();

	bool should_update = elapsed_seconds > 0;
	bool turn_backlight_on = false;

	// check the water level sensor
	// note: the condition is reset only manually
	if (!tank_empty && false) // digitalRead(sensor))
	{
		tank_empty = true;
		should_update = true;
	}

	update_feed(elapsed_seconds);

	/*
	if (ui.state == UI::SELECT)
	{
		if (enc.isLeft())
		{
			if (ui.selected % PAGE_SIZE == 0 && ui.page > 0)
			{
				// switch to the previous page
				ui.page--;
				ui.selected--;
			}
			else if (ui.selected > 0)
			{
				// select the previous widget
				ui.selected--;
			}

			turn_backlight_on = true;
		}
		else if (enc.isRight())
		{
			if (ui.selected % PAGE_SIZE == WIDGETS_COUNT - 1 && ui.page < PAGES - 1)
			{
				ui.page++;
				ui.selected++;
			}
			else if (ui.selected < WIDGETS_COUNT - 1)
			{
				ui.selected++;
			}

			turn_backlight_on = true;
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
					if (w.on_click)
					{
						w.on_click(w);
					}
					break;
			}

			turn_backlight_on = true;
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
			if (w.on_set)
			{
				w.on_set(w, increment);
				should_update = true;
			}
		}

		if (enc.isClick())
		{
			ui.state = UI::SELECT;
			should_update = true;

			save_values_to_eeprom();
		}
	}
	*/

	if (should_update)
	{
		if (turn_backlight_on)
		{
			backlight_remaining_seconds = BACKLIGHT_TIMEOUT;
		}
		update_ui();
	}
}

void update_duration(Widget &w)
{
	static char units[] = {'d', 'h', 'm', 's'};
	static unsigned long thresholds[] = {DAY, HOUR, MINUTE, SECOND};
	unsigned long value = *(unsigned long *)w.field.value;

	if (!w.label)
	{
		w.label = new char[5];
	}

	for (int i = 0; i < 4; i++)
	{
		if (value >= thresholds[i])
		{
			value = ceil(value / (double)thresholds[i]);
			snprintf(w.label, 4, "%02lu%c", value, units[i]);
			break;
		}
	}
}

void update_time(Widget &w)
{
	if (!w.label)
	{
		w.label = new char[4];
	}
	seconds t = *(seconds*)w.field.value;
	unsigned hours = t / HOUR;
	snprintf(w.label, 4, "%02uh", hours);
}

void set_time(Widget &w, int increment)
{

}

void set_duration(Widget &w, int increment)
{
	unsigned long *dst = (unsigned long *)w.field.value;
	unsigned long value = *dst;
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

	*dst = constrain(value, w.field.min, w.field.max);
}

void update_percentile(Widget &w)
{
	if (!w.label)
	{
		w.label = (char*)malloc(4);
	}
	snprintf(w.label, 4, "%02lu%%", *(byte*)w.field.value);
}

void update_feed_button(Widget &w)
{
	if (!w.label)
	{
		w.label = (char*)malloc(4);
	}

	if (tank_empty)
	{
		w.icon = FILL_CHR;
		snprintf(w.label, 4, label_fill);
	}
	else
	{
		w.icon = feed_active ? STOP_CHR : WATER_CHR;
		if (feed_active)
		{
			snprintf(w.label, 4, label_stop);
		}
		else
		{
			snprintf(w.label, 4, label_feed);
		}
	}
}

void set_pwm(Widget &w, int increment)
{
	unsigned long v = *(unsigned long *)w.field.value;
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

	*(unsigned long *)w.field.value = constrain(v, w.field.min, w.field.max);

	update_pwm();
}

void toggle_feed(Widget &w)
{
	if (tank_empty)
	{
		tank_empty = false; // digitalRead(sensor);
	}
	else
	{
		// just zero the countdown and call the feed update function
		feed_countdown = 0;
		update_feed(0);
	}
}

void update_pwm()
{
	analogWrite(pwm, (feed_active && !tank_empty) * (feed_intensity / 100.0) * 1024);
}

void update_feed(unsigned dt)
{
	if (!tank_empty)
	{
		if (feed_countdown > dt)
		{
			// some seconds still left
			feed_countdown -= dt;
		}
		else
		{
			// countdown finished
			if (feed_active)
			{
				// feeding finished, stop and switch to countdown
				feed_countdown = active_phase->interval;
			}
			else
			{
				// countdown finished, start feeding
				feed_countdown = active_phase->duration;
			}
			feed_active = !feed_active;
		}
	}

	update_pwm();
}

void save_values_to_eeprom()
{
	EEStore ee = {
		.magic = EEMAGIC,
		// .feed_period = feed_period,
		// .feed_intensity = feed_intensity,
		// .feed_duration = feed_duration,
	};
	EEPROM.put(0, ee);
}

void load_values_from_eeprom()
{
	// load the values only if if the magic signature is present
	EEStore ee;
	EEPROM.get(0, ee);
	if (ee.magic == EEMAGIC)
	{
		// feed_period = feed_countdown = ee.feed_period;
		// feed_duration = ee.feed_duration;
		// feed_intensity = ee.feed_intensity;
	}
}
