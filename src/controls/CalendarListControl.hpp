#pragma once

#include "./IBaseControl.hpp"
#include "./CalendarItemControl.hpp"

#include "../ics/ics_parse.hpp"
#include "../safe_function.h"

#include "../VectorBackend.h"

#include <SDL2/SDL.h>
#include <curl/curl.h>

#include <vector>
#include <chrono>
#include <thread>
#include <future>

#define NUMBER_OF_EVENTS_PER_PAGE (7)
#define CALENDAR_ITEM_HEIGHT (14*3 + 4)
#define CALENDAR_DATE_HEIGHT (15)
#define CALENDAR_REPAINT_INTERVAL (std::chrono::minutes(10))
#define CALENDAR_FETCH_INTERVAL (std::chrono::hours(6))

class CalendarListControl : public IBaseControl {
private:
  std::mutex m_update_mutex;
  unsigned int m_label_render_size{0};
  std::vector<CalendarItemControl*> m_items;
  IcsParse* ics_parser = nullptr;

  std::vector<LabelControl*> m_date_labels;
  std::map<int, CalendarTime> m_dates;

public:
  void update_calendar()
  {
    std::lock_guard<std::mutex> update_mutex_guard(m_update_mutex);

    printf("update_calendar...\n");
    m_dates.clear();

    SDL_Rect p = SDL_Rect{
      m_pos.x, m_pos.y,
      m_pos.w, CALENDAR_ITEM_HEIGHT
    };
    
    time_t tt;
    tm local{};

    time(&tt);
    Safe::localtime(&local, &tt);

    const int year = 1900 + local.tm_year;
    const int month = local.tm_mon + 1;
    int day = local.tm_mday;

    uint32_t now_stamp = getCalendarStamp(year, month, day, local.tm_hour, local.tm_min);

    printf("now_stamp = %u\n", now_stamp);
    
    CalendarEvent event{};
    ics_parser->restart();

    for(unsigned int i = 0; i < NUMBER_OF_EVENTS_PER_PAGE; i++)
    {
      if (i >= m_items.size())
      {
        m_items.push_back(new CalendarItemControl(&p));
      }
      
      const bool found = ics_parser->next_event(event, [&now_stamp](CalendarEvent& e)->bool
      {
        return getCalendarStamp(e.start) >= now_stamp;
      });

      printf("found event: (%d); title = %s at %02d/%02d/%04d\n", found, event.summary, event.start.day, event.start.month, event.start.year);

      if (found)
      {
        if (day != event.start.day)
        {
          day = event.start.day;
          m_dates.insert(std::pair<int, CalendarTime>(p.y, event.start));
          p.y = p.y + CALENDAR_DATE_HEIGHT - 1;
        }

        m_items.at(i)->show();
        m_items.at(i)->set_calender(event);
        m_items.at(i)->set_pos_ref(p);
      } else
      {
        m_items.at(i)->hide();
      }

      p.y = p.y + CALENDAR_ITEM_HEIGHT - 1;
    }

    unsigned int date_label_index = 0;
    char buffer[1024];
    p.x += 8;
    p.h = CALENDAR_DATE_HEIGHT;
    
    for (auto && date : m_dates)
    {
      if (date_label_index >= m_date_labels.size())
      {
        auto label = new LabelControl(&p);
        label->set_font(FontManager::get_instance()->get_font("default", 12));
        label->set_align(LabelControl::LabelAlign::LEFT);
        m_date_labels.push_back(label);
      }
      p.y = date.first;
      snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d", date.second.day, date.second.month, date.second.year);
      printf("date.first(y pos)=%d; text=%s\n", date.first, buffer);
      m_date_labels.at(date_label_index)->set_pos(p);
      m_date_labels.at(date_label_index)->set_text(buffer);

      date_label_index++;
    }

    m_label_render_size = date_label_index;

    // Update every 10 mins
    std::thread([this](){
      std::this_thread::sleep_for(CALENDAR_REPAINT_INTERVAL);
      this->update_calendar();
    }).detach();
  }

  static size_t write_to_vector(uint8_t *ptr, size_t size, size_t nmemb, std::vector<uint8_t> *s) {
    // static std::ofstream f_out("debug_out.txt", std::ofstream::binary);

    size_t realsize = size * nmemb;
    // f_out.write((char*)ptr, realsize);
    s->insert(s->end(), ptr, ptr + realsize);
    return realsize;
  }

  void download_calendar()
  {
    // Update every 24 hours
    std::thread([this]() {
      std::this_thread::sleep_for(CALENDAR_FETCH_INTERVAL);
      this->download_calendar();
    }).detach();

    CURL *curl = curl_easy_init();
    if (curl)
    {
      std::ifstream f("calendar.txt");

      std::vector<uint8_t> result;
      char buffer_url[255] = {0};
      f.read(buffer_url, sizeof(buffer_url));
      f.close();
      printf("buffer_url: %s\n", buffer_url);

      curl_easy_setopt(curl, CURLOPT_URL, buffer_url);
      /* SSL Options */
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1);
      curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem");

      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_vector);
      CURLcode res = curl_easy_perform(curl);
      curl_easy_cleanup(curl);

      if (res != CURLE_OK)
      {
        printf("curl error: %s\n", curl_easy_strerror(res));
      }

      {
        std::lock_guard<std::mutex> update_mutex_guard(m_update_mutex);
        backend->assign(result);
      }
    }
  }

  CalendarListControl(SDL_Rect* pos = nullptr): IBaseControl(pos) {
    // ics_parser = new IcsParse("./ical.ics");
    backend = new VectorBackend();
    ics_parser = new IcsParse(backend);

    download_calendar();
    update_calendar();
  }

  ~CalendarListControl()
  {
    free_ics_parser();
  }

  void render() override
  {
    std::lock_guard<std::mutex> update_mutex_guard(m_update_mutex);
    for (auto && item : m_items)
    {
      item->render();
    }
    
    SDL_SetRenderDrawColor(get_renderer(), 0xFF, 0xFF, 0xFF, 0xFF);
    for(unsigned int i = 0; i < m_label_render_size; i++)
    {
      SDL_Rect pos = m_date_labels.at(i)->get_pos();
      pos.x = m_pos.x;
      pos.w = m_pos.w;
      SDL_RenderDrawRect(get_renderer(), &pos);
      m_date_labels.at(i)->render();
    }
  }

private:
  VectorBackend* backend = nullptr;
  void free_ics_parser()
  {
    if (ics_parser)
    {
      free(ics_parser);
      ics_parser = nullptr;
    }
  }
};
